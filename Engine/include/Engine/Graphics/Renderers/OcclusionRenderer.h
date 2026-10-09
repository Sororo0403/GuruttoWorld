#pragma once
#include <Engine/Graphics/ShaderCompiler.h>
#include <Engine/Graphics/Resources/RenderFrameContext.h>
#include <Engine/Graphics/Resources/DepthBuffer.h>
#include <DirectXCollision.h>
#include <wrl/client.h>
#include <limits>
#include <memory>
#include <vector>

namespace Engine
{
    class OcclusionRenderer final
    {
    public:
        /// <summary>色と深度を変更しない境界箱のGPU遮蔽判定パイプラインを生成します。</summary>
        bool Initialize(ID3D12Device* device,const std::filesystem::path& shader)
        {
            if (!device || device_) return false;
            D3D12_ROOT_PARAMETER parameter{}; parameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            parameter.Constants={4,0,24}; parameter.ShaderVisibility=D3D12_SHADER_VISIBILITY_VERTEX;
            D3D12_ROOT_SIGNATURE_DESC description{}; description.NumParameters=1; description.pParameters=&parameter;
            Microsoft::WRL::ComPtr<ID3DBlob> serialized,errors,vertex;
            if (FAILED(D3D12SerializeRootSignature(&description,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&errors)) ||
                FAILED(device->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),IID_PPV_ARGS(&root_))) ||
                !CompileShader(shader,"VSBounds","vs_5_0",vertex)) return false;
            D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{}; pipeline.pRootSignature=root_.Get();
            pipeline.VS={vertex->GetBufferPointer(),vertex->GetBufferSize()};
            pipeline.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID; pipeline.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
            pipeline.RasterizerState.DepthClipEnable=TRUE;
            pipeline.BlendState.RenderTarget[0].RenderTargetWriteMask=0;
            auto& blend=pipeline.BlendState.RenderTarget[0]; blend.SrcBlend=blend.SrcBlendAlpha=D3D12_BLEND_ONE;
            blend.DestBlend=blend.DestBlendAlpha=D3D12_BLEND_ZERO; blend.BlendOp=blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;
            blend.LogicOp=D3D12_LOGIC_OP_NOOP;
            pipeline.DepthStencilState.DepthEnable=TRUE; pipeline.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;
            pipeline.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_LESS_EQUAL;
            auto& stencil=pipeline.DepthStencilState.FrontFace;
            stencil.StencilFailOp=stencil.StencilDepthFailOp=stencil.StencilPassOp=D3D12_STENCIL_OP_KEEP;
            stencil.StencilFunc=D3D12_COMPARISON_FUNC_ALWAYS; pipeline.DepthStencilState.BackFace=stencil;
            pipeline.SampleMask=UINT_MAX; pipeline.SampleDesc.Count=1; pipeline.DSVFormat=DepthBuffer::Format;
            pipeline.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            if (FAILED(device->CreateGraphicsPipelineState(&pipeline,IID_PPV_ARGS(&pipeline_)))) return false;
            device_=device; return true;
        }
        /// <summary>現フレームの境界箱を深度へ照合し、後続の描画をGPUの結果で条件化します。</summary>
        bool Begin(ID3D12GraphicsCommandList* commands,const DirectX::BoundingBox& bounds,
            const DirectX::XMFLOAT4X4& world,const DirectX::XMFLOAT4X4& viewProjection)
        {
            RenderFrameContext context;
            if (!device_ || !RenderFrameContext::Current(commands,context)) return false;
            RenderTargetBinding target;
            if (!RenderTargetBinding::Current(commands,target) || !target.depth.ptr) return false;
            const auto transform=DirectX::XMLoadFloat4x4(&world)*DirectX::XMLoadFloat4x4(&viewProjection);
            std::array<DirectX::XMFLOAT3,8> corners; bounds.GetCorners(corners.data());
            for (const auto& corner : corners)
            {
                const auto clip=DirectX::XMVector4Transform(DirectX::XMVectorSet(corner.x,corner.y,corner.z,1),transform);
                if (DirectX::XMVectorGetW(clip)<=.001f || DirectX::XMVectorGetZ(clip)<=0) return false;
            }
            auto& frame=frames_[context.slot];
            if (frame.serial!=context.serial) { frame.serial=context.serial; frame.used=0; }
            if (frame.used>=4096) return false;
            if (frame.used==frame.entries.size())
            {
                auto candidate=std::make_unique<Entry>(); if (!candidate->Initialize(device_.Get())) return false;
                frame.entries.push_back(std::move(candidate));
            }
            auto& entry=*frame.entries[frame.used++];
            if (entry.predication) Transition(commands,entry.buffer.Get(),D3D12_RESOURCE_STATE_PREDICATION,D3D12_RESOURCE_STATE_COPY_DEST);
            struct Constants { DirectX::XMFLOAT4X4 transform; DirectX::XMFLOAT4 minimum,maximum; } constants;
            DirectX::XMStoreFloat4x4(&constants.transform,transform);
            constants.minimum={bounds.Center.x-bounds.Extents.x,bounds.Center.y-bounds.Extents.y,bounds.Center.z-bounds.Extents.z,0};
            constants.maximum={bounds.Center.x+bounds.Extents.x,bounds.Center.y+bounds.Extents.y,bounds.Center.z+bounds.Extents.z,0};
            commands->SetPipelineState(pipeline_.Get()); commands->SetGraphicsRootSignature(root_.Get());
            commands->SetGraphicsRoot32BitConstants(0,24,&constants,0);
            commands->OMSetRenderTargets(0,nullptr,FALSE,&target.depth);
            commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            commands->BeginQuery(entry.query.Get(),D3D12_QUERY_TYPE_BINARY_OCCLUSION,0);
            commands->DrawInstanced(36,1,0,0);
            commands->EndQuery(entry.query.Get(),D3D12_QUERY_TYPE_BINARY_OCCLUSION,0);
            commands->ResolveQueryData(entry.query.Get(),D3D12_QUERY_TYPE_BINARY_OCCLUSION,0,1,entry.buffer.Get(),0);
            target.Bind(commands);
            Transition(commands,entry.buffer.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PREDICATION);
            entry.predication=true; commands->SetPredication(entry.buffer.Get(),0,D3D12_PREDICATION_OP_EQUAL_ZERO); return true;
        }
        /// <summary>次の物体やUIへ判定を持ち越さないよう条件付き描画を解除します。</summary>
        static void End(ID3D12GraphicsCommandList* commands)
        { commands->SetPredication(nullptr,0,D3D12_PREDICATION_OP_EQUAL_ZERO); }
    private:
        /// <summary>判定領域の書き込みとGPU条件参照を同期します。</summary>
        static void Transition(ID3D12GraphicsCommandList* commands,ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after)
        {
            D3D12_RESOURCE_BARRIER barrier{}; barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after}; commands->ResourceBarrier(1,&barrier);
        }
        struct Entry
        {
            Microsoft::WRL::ComPtr<ID3D12QueryHeap> query;
            Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
            bool predication=false;
            /// <summary>フレームのフェンス完了後だけ再利用するクエリと結果領域を作成します。</summary>
            bool Initialize(ID3D12Device* device)
            {
                D3D12_QUERY_HEAP_DESC queryDescription{}; queryDescription.Type=D3D12_QUERY_HEAP_TYPE_OCCLUSION; queryDescription.Count=1;
                D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_DEFAULT;
                D3D12_RESOURCE_DESC description{}; description.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; description.Width=8;
                description.Height=1; description.DepthOrArraySize=description.MipLevels=1; description.SampleDesc.Count=1;
                description.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                return SUCCEEDED(device->CreateQueryHeap(&queryDescription,IID_PPV_ARGS(&query))) &&
                    SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&description,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&buffer)));
            }
        };
        struct Frame { UINT64 serial=(std::numeric_limits<UINT64>::max)(); size_t used=0; std::vector<std::unique_ptr<Entry>> entries; };
        std::array<Frame,RenderFrameContext::SlotCount> frames_;
        Microsoft::WRL::ComPtr<ID3D12Device> device_;
        Microsoft::WRL::ComPtr<ID3D12RootSignature> root_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;
    };
}
