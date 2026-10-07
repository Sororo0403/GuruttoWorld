#include <Engine/Graphics/Renderers/ShadowMap.h>
#include <Engine/Graphics/ShaderCompiler.h>
#include <Engine/Graphics/Models/MeshData.h>
#include <Engine/Core/Log.h>
#include <format>
#include <d3d12sdklayers.h>
#include <algorithm>
#include <cmath>

namespace Engine
{
    namespace
    {
        void ReportPipelineFailure(ID3D12Device* device,HRESULT result)
        {
            Log::Error(std::format("Shadow depth pipeline failed: 0x{:08X}",static_cast<unsigned long>(result)));
            Microsoft::WRL::ComPtr<ID3D12InfoQueue> messages;
            if (FAILED(device->QueryInterface(IID_PPV_ARGS(&messages)))) return;
            for (UINT64 i=0;i<messages->GetNumStoredMessagesAllowedByRetrievalFilter();++i)
            {
                SIZE_T bytes=0; messages->GetMessage(i,nullptr,&bytes);
                std::vector<unsigned char> buffer(bytes);
                auto* message=reinterpret_cast<D3D12_MESSAGE*>(buffer.data());
                if (SUCCEEDED(messages->GetMessage(i,message,&bytes)) && message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR)
                    Log::Error(message->pDescription);
            }
        }
    }
    bool ShadowMap::CreateDepth(ID3D12Device* device)
    {
        D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC resource{};
        resource.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        resource.Width=resource.Height=Resolution; resource.DepthOrArraySize=resource.MipLevels=1;
        resource.Format=DXGI_FORMAT_R32_TYPELESS; resource.SampleDesc.Count=1;
        resource.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE clear{}; clear.Format=DXGI_FORMAT_D32_FLOAT; clear.DepthStencil.Depth=1;
        if (FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&resource,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,&clear,IID_PPV_ARGS(&depth_)))) return false;
        D3D12_DESCRIPTOR_HEAP_DESC descriptors{}; descriptors.NumDescriptors=1;
        descriptors.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        if (FAILED(device->CreateDescriptorHeap(&descriptors,IID_PPV_ARGS(&dsv_)))) return false;
        descriptors.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        if (FAILED(device->CreateDescriptorHeap(&descriptors,IID_PPV_ARGS(&srv_)))) return false;
        D3D12_DEPTH_STENCIL_VIEW_DESC depthView{};
        depthView.Format=DXGI_FORMAT_D32_FLOAT; depthView.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2D;
        device->CreateDepthStencilView(depth_.Get(),&depthView,dsv_->GetCPUDescriptorHandleForHeapStart());
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format=DXGI_FORMAT_R32_FLOAT; view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
        view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; view.Texture2D.MipLevels=1;
        device->CreateShaderResourceView(depth_.Get(),&view,srv_->GetCPUDescriptorHandleForHeapStart());
        return true;
    }
    bool ShadowMap::CreatePipeline(ID3D12Device* device,const std::filesystem::path& shader)
    {
        D3D12_ROOT_PARAMETER parameter{}; parameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameter.Constants.ShaderRegister=3;
        parameter.Constants.Num32BitValues=16; parameter.ShaderVisibility=D3D12_SHADER_VISIBILITY_VERTEX;
        D3D12_ROOT_SIGNATURE_DESC signature{};
        signature.NumParameters=1; signature.pParameters=&parameter;
        signature.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        Microsoft::WRL::ComPtr<ID3DBlob> bytes,errors,vertex;
        if (FAILED(D3D12SerializeRootSignature(&signature,D3D_ROOT_SIGNATURE_VERSION_1,&bytes,&errors)) ||
            FAILED(device->CreateRootSignature(0,bytes->GetBufferPointer(),bytes->GetBufferSize(),IID_PPV_ARGS(&root_))) ||
            !CompileShader(shader,"VSShadow","vs_5_0",vertex)) return false;
        const D3D12_INPUT_ELEMENT_DESC element{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,
            static_cast<UINT>(offsetof(MeshVertex,position)),D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0};
        D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
        description.pRootSignature=root_.Get(); description.VS={vertex->GetBufferPointer(),vertex->GetBufferSize()};
        description.InputLayout={&element,1};
        description.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;
        // Both sides cast shadows, including mirrored transforms and thin CC0 sign geometry.
        description.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
        description.RasterizerState.DepthClipEnable=TRUE;
        description.RasterizerState.DepthBias=100;
        description.RasterizerState.SlopeScaledDepthBias=1;
        description.RasterizerState.DepthBiasClamp=.002f;
        description.DepthStencilState.DepthEnable=TRUE;
        description.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;
        description.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_LESS;
        auto& face=description.DepthStencilState.FrontFace;
        face.StencilFailOp=face.StencilDepthFailOp=face.StencilPassOp=D3D12_STENCIL_OP_KEEP;
        face.StencilFunc=D3D12_COMPARISON_FUNC_ALWAYS;
        description.DepthStencilState.BackFace=face;
        description.SampleMask=UINT_MAX; description.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        description.DSVFormat=DXGI_FORMAT_D32_FLOAT; description.SampleDesc.Count=1;
        auto& blend=description.BlendState.RenderTarget[0];
        blend.SrcBlend=blend.SrcBlendAlpha=D3D12_BLEND_ONE;
        blend.DestBlend=blend.DestBlendAlpha=D3D12_BLEND_ZERO;
        blend.BlendOp=blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;
        blend.LogicOp=D3D12_LOGIC_OP_NOOP;
        blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
        const auto result=device->CreateGraphicsPipelineState(&description,IID_PPV_ARGS(&pipeline_));
        if (FAILED(result)) ReportPipelineFailure(device,result);
        return SUCCEEDED(result);
    }
    bool ShadowMap::Initialize(ID3D12Device* device,const std::filesystem::path& shader)
    {
        return device && !depth_ && CreateDepth(device) && CreatePipeline(device,shader);
    }
    void ShadowMap::Transition(ID3D12GraphicsCommandList* commands,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after)
    {
        D3D12_RESOURCE_BARRIER barrier{}; barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource=depth_.Get(); barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore=before; barrier.Transition.StateAfter=after; commands->ResourceBarrier(1,&barrier);
    }
    bool ShadowMap::Begin(ID3D12GraphicsCommandList* commands,const Camera& camera,const DirectionalLight& light)
    {
        using namespace DirectX;
        if (!depth_ || !std::isfinite(light.shadowDistance) || !std::isfinite(light.shadowBias) ||
            !RenderTargetBinding::Current(commands,previous_)) return false;
        const auto direction=XMVectorSet(light.direction[0],light.direction[1],light.direction[2],0);
        const float length=XMVectorGetX(XMVector3LengthSq(direction));
        if (!std::isfinite(length) || length<.00000001f) return false;
        const auto z=XMVector3Normalize(direction);
        if (!std::isfinite(XMVectorGetX(z))) return false;
        const auto reference=std::abs(XMVectorGetY(z))>.95f ? XMVectorSet(0,0,1,0) : XMVectorSet(0,1,0,0);
        const auto x=XMVector3Normalize(XMVector3Cross(reference,z));
        const auto y=XMVector3Cross(z,x);
        const float radius=std::clamp(light.shadowDistance,10.0f,200.0f),texel=2*radius/Resolution;
        const auto inverseView=XMMatrixInverse(nullptr,camera.GetViewMatrix());
        auto center=inverseView.r[3]+inverseView.r[2]*(radius*.4f)+XMVectorSet(0,radius*.25f,0,0);
        // Align light-space XY to texels so camera motion does not crawl across fixed surfaces.
        center+=x*(std::round(XMVectorGetX(XMVector3Dot(center,x))/texel)*texel-XMVectorGetX(XMVector3Dot(center,x)));
        center+=y*(std::round(XMVectorGetX(XMVector3Dot(center,y))/texel)*texel-XMVectorGetX(XMVector3Dot(center,y)));
        XMStoreFloat4x4(&viewProjection_,XMMatrixLookToLH(center-z*(2*radius),z,y)*XMMatrixOrthographicLH(2*radius,2*radius,0,4*radius));
        constants_={XMVectorGetX(center),XMVectorGetY(center),XMVectorGetZ(center),radius,light.shadowBias,1.0f/Resolution};
        Transition(commands,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_DEPTH_WRITE);
        const auto depth=dsv_->GetCPUDescriptorHandleForHeapStart();
        commands->ClearDepthStencilView(depth,D3D12_CLEAR_FLAG_DEPTH,1,0,0,nullptr);
        RenderTargetBinding{{},depth,{0,0,float(Resolution),float(Resolution),0,1},{0,0,Resolution,Resolution}}.Bind(commands);
        return true;
    }
    void ShadowMap::End(ID3D12GraphicsCommandList* commands)
    {
        commands->OMSetRenderTargets(0,nullptr,FALSE,nullptr);
        Transition(commands,D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        previous_.Bind(commands);
    }
    void ShadowMap::Draw(ID3D12GraphicsCommandList* commands,const DirectX::XMFLOAT4X4& world,
        const D3D12_VERTEX_BUFFER_VIEW& vertices,const D3D12_INDEX_BUFFER_VIEW& indices,UINT count) const
    {
        using namespace DirectX;
        XMFLOAT4X4 matrix; XMStoreFloat4x4(&matrix,XMLoadFloat4x4(&world)*XMLoadFloat4x4(&viewProjection_));
        commands->SetPipelineState(pipeline_.Get()); commands->SetGraphicsRootSignature(root_.Get());
        commands->SetGraphicsRoot32BitConstants(0,16,&matrix,0);
        commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commands->IASetVertexBuffers(0,1,&vertices); commands->IASetIndexBuffer(&indices);
        commands->DrawIndexedInstanced(count,1,0,0,0);
    }
}
