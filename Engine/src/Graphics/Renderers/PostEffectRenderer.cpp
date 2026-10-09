#include <Engine/Graphics/Renderers/PostEffectRenderer.h>
#include <Engine/Graphics/Resources/RenderTexture.h>
#include <Engine/Graphics/Resources/RenderTargetBinding.h>
#include <Engine/Graphics/Resources/RenderFrameContext.h>
#include <Engine/Graphics/ShaderCompiler.h>
#include <Engine/Core/Log.h>
#include <wrl/client.h>
#include <algorithm>
#include <limits>
#include <vector>

namespace Engine
{
    struct PostEffectRenderer::Impl
    {
        struct Targets
        {
            RenderTexture scene,bright,temporary,meter;
            std::array<Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>,4> bindings;
            bool Initialize(ID3D12Device* gpuDevice,UINT width,UINT height)
            {
                const UINT halfWidth=std::max(1U,(width+1)/2),halfHeight=std::max(1U,(height+1)/2);
                if (!scene.Initialize(gpuDevice,width,height,RenderTexture::HdrFormat) ||
                    !bright.Initialize(gpuDevice,halfWidth,halfHeight,RenderTexture::HdrFormat) ||
                    !temporary.Initialize(gpuDevice,halfWidth,halfHeight,RenderTexture::HdrFormat) ||
                    !meter.Initialize(gpuDevice,1,1,RenderTexture::HdrFormat)) return false;
                const std::array<RenderTexture*,4> sources{&scene,&scene,&bright,&temporary};
                D3D12_DESCRIPTOR_HEAP_DESC description{}; description.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                description.NumDescriptors=3; description.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                for (size_t index=0;index<bindings.size();++index)
                {
                    if (FAILED(gpuDevice->CreateDescriptorHeap(&description,IID_PPV_ARGS(&bindings[index])))) return false;
                    auto handle=bindings[index]->GetCPUDescriptorHandleForHeapStart();
                    gpuDevice->CopyDescriptorsSimple(1,handle,sources[index]->GetShaderResourceView(),description.Type);
                    handle.ptr+=gpuDevice->GetDescriptorHandleIncrementSize(description.Type);
                    if (index==0) gpuDevice->CopyDescriptorsSimple(1,handle,bright.GetShaderResourceView(),description.Type);
                    else
                    {
                        D3D12_SHADER_RESOURCE_VIEW_DESC empty{}; empty.Format=RenderTexture::HdrFormat;
                        empty.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D; empty.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                        empty.Texture2D.MipLevels=1; gpuDevice->CreateShaderResourceView(nullptr,&empty,handle);
                    }
                    handle.ptr+=gpuDevice->GetDescriptorHandleIncrementSize(description.Type);
                    if (index==0) gpuDevice->CopyDescriptorsSimple(1,handle,meter.GetShaderResourceView(),description.Type);
                    else
                    {
                        D3D12_SHADER_RESOURCE_VIEW_DESC empty{}; empty.Format=RenderTexture::HdrFormat;
                        empty.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D; empty.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                        empty.Texture2D.MipLevels=1; gpuDevice->CreateShaderResourceView(nullptr,&empty,handle);
                    }
                }
                return true;
            }
        };
        struct Frame
        {
            UINT64 serial=std::numeric_limits<UINT64>::max(); size_t used=0;
            std::vector<std::unique_ptr<Targets>> entries;
        };
        Microsoft::WRL::ComPtr<ID3D12Device> device;
        Microsoft::WRL::ComPtr<ID3D12RootSignature> root;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> ldr,hdr;
        std::array<Frame,RenderFrameContext::SlotCount> frames;
        Targets* active=nullptr; ID3D12GraphicsCommandList* commands=nullptr; RenderTargetBinding prior;
        bool Initialize(ID3D12Device* input,const std::filesystem::path& shader)
        {
            if (!input) return false;
            D3D12_DESCRIPTOR_RANGE range{}; range.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV; range.NumDescriptors=3;
            D3D12_ROOT_PARAMETER parameters[2]{};
            parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameters[0].DescriptorTable={1,&range}; parameters[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
            parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            parameters[1].Constants.Num32BitValues=20; parameters[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
            D3D12_STATIC_SAMPLER_DESC sampler{}; sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS; sampler.MaxLOD=D3D12_FLOAT32_MAX;
            sampler.MaxAnisotropy=1; sampler.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
            D3D12_ROOT_SIGNATURE_DESC signature{}; signature.NumParameters=2; signature.pParameters=parameters;
            signature.NumStaticSamplers=1; signature.pStaticSamplers=&sampler;
            Microsoft::WRL::ComPtr<ID3DBlob> blob,errors,vertex,pixel;
            if (FAILED(D3D12SerializeRootSignature(&signature,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&errors)) ||
                FAILED(input->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root))) ||
                !CompileShader(shader,"VSMain","vs_5_0",vertex) || !CompileShader(shader,"PSMain","ps_5_0",pixel)) return false;
            D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{}; pipeline.pRootSignature=root.Get();
            pipeline.VS={vertex->GetBufferPointer(),vertex->GetBufferSize()}; pipeline.PS={pixel->GetBufferPointer(),pixel->GetBufferSize()};
            pipeline.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID; pipeline.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
            pipeline.RasterizerState.DepthClipEnable=TRUE;
            auto& blend=pipeline.BlendState.RenderTarget[0]; blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
            blend.SrcBlend=blend.SrcBlendAlpha=D3D12_BLEND_ONE; blend.DestBlend=blend.DestBlendAlpha=D3D12_BLEND_ZERO;
            blend.BlendOp=blend.BlendOpAlpha=D3D12_BLEND_OP_ADD; blend.LogicOp=D3D12_LOGIC_OP_NOOP;
            pipeline.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS; pipeline.SampleMask=UINT_MAX;
            pipeline.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; pipeline.NumRenderTargets=1;
            pipeline.RTVFormats[0]=RenderTexture::Format; pipeline.DSVFormat=DepthBuffer::Format; pipeline.SampleDesc.Count=1;
            if (FAILED(input->CreateGraphicsPipelineState(&pipeline,IID_PPV_ARGS(&ldr)))) return false;
            pipeline.RTVFormats[0]=RenderTexture::HdrFormat;
            if (FAILED(input->CreateGraphicsPipelineState(&pipeline,IID_PPV_ARGS(&hdr)))) return false;
            device=input; return true;
        }
        void Draw(ID3D12GraphicsCommandList* list,ID3D12DescriptorHeap* binding,const PostEffectSettings& settings,float pass,
            UINT width,UINT height,bool hdrOutput) const
        {
            const std::array<float,20> constants{settings.exposure,settings.bloomIntensity,settings.bloomThreshold,
                static_cast<float>(settings.toneMapping),1.0f/width,1.0f/height,pass,hdrOutput ? 1.0f : 0.0f,settings.bloomRadius,
                settings.bloomEnabled ? 1.0f : 0.0f,settings.autoExposure ? 1.0f : 0.0f,settings.middleGray,
                settings.exposureMinimum,settings.exposureMaximum,settings.contrast,settings.saturation,
                settings.colorFilter[0],settings.colorFilter[1],settings.colorFilter[2],0};
            list->SetPipelineState(hdrOutput ? hdr.Get() : ldr.Get()); list->SetGraphicsRootSignature(root.Get());
            list->SetDescriptorHeaps(1,&binding); list->SetGraphicsRootDescriptorTable(0,binding->GetGPUDescriptorHandleForHeapStart());
            list->SetGraphicsRoot32BitConstants(1,20,constants.data(),0); list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            list->DrawInstanced(3,1,0,0);
        }
    };
    PostEffectRenderer::PostEffectRenderer()=default;
    PostEffectRenderer::~PostEffectRenderer()=default;
    bool PostEffectRenderer::Ready() const noexcept { return static_cast<bool>(impl_); }
    bool PostEffectRenderer::Initialize(ID3D12Device* device,const std::filesystem::path& shader)
    {
        if (impl_) return false;
        auto candidate=std::make_unique<Impl>(); if (!candidate->Initialize(device,shader)) return false;
        impl_=std::move(candidate); return true;
    }
    bool PostEffectRenderer::Begin(ID3D12GraphicsCommandList* commands,UINT width,UINT height,const std::array<float,4>& clear)
    {
        RenderFrameContext context; RenderTargetBinding prior;
        if (!impl_ || impl_->active || !width || !height || !RenderFrameContext::Current(commands,context) ||
            !RenderTargetBinding::Current(commands,prior) || !prior.color.ptr) return false;
        auto& frame=impl_->frames[context.slot]; if (frame.serial!=context.serial) { frame.serial=context.serial; frame.used=0; }
        if (frame.used>=16) return false;
        if (frame.used==frame.entries.size()) frame.entries.push_back(nullptr);
        auto& entry=frame.entries[frame.used];
        if (!entry || entry->scene.GetWidth()!=width || entry->scene.GetHeight()!=height)
        {
            auto candidate=std::make_unique<Impl::Targets>();
            if (!candidate->Initialize(impl_->device.Get(),width,height)) return false;
            // この枠はBeginFrameのフェンス待機済みで、同一フレームの先の描画とは別の領域です。
            entry=std::move(candidate);
        }
        if (!entry->scene.Begin(commands,clear)) return false;
        impl_->active=entry.get(); impl_->commands=commands; impl_->prior=prior; ++frame.used; return true;
    }
    bool PostEffectRenderer::End(ID3D12GraphicsCommandList* commands,const PostEffectSettings& settings)
    {
        if (!impl_ || !impl_->active || impl_->commands!=commands) return false;
        auto& targets=*impl_->active;
        if (!targets.scene.End(commands)) return false;
        const bool valid=settings.Valid();
        bool processed=valid;
        if (valid && settings.autoExposure)
        {
            processed=targets.meter.Begin(commands,{0,0,0,1});
            if (processed)
            {
                impl_->Draw(commands,targets.bindings[1].Get(),settings,4,targets.scene.GetWidth(),targets.scene.GetHeight(),true);
                processed=targets.meter.End(commands);
            }
        }
        if (valid && settings.bloomEnabled && settings.bloomIntensity>0)
        {
            const auto pass=[&](RenderTexture& output,const RenderTexture& source,size_t binding,float mode) {
                if (!output.Begin(commands,{0,0,0,1})) return false;
                impl_->Draw(commands,targets.bindings[binding].Get(),settings,mode,source.GetWidth(),source.GetHeight(),true);
                return output.End(commands);
            };
            processed=processed && pass(targets.bright,targets.scene,1,1) && pass(targets.temporary,targets.bright,2,2) &&
                pass(targets.bright,targets.temporary,3,3);
        }
        impl_->prior.Bind(commands);
        auto applied=valid ? settings : PostEffectSettings{};
        if (!processed) applied.bloomEnabled=false;
        if (!processed) applied.autoExposure=false;
        if (!valid) applied.toneMapping=ToneMapping::None;
        if (applied.bloomIntensity==0) applied.bloomEnabled=false;
        impl_->Draw(commands,targets.bindings[0].Get(),applied,0,targets.scene.GetWidth(),targets.scene.GetHeight(),impl_->prior.format==RenderTexture::HdrFormat);
        impl_->active=nullptr; impl_->commands=nullptr; return processed;
    }
}
