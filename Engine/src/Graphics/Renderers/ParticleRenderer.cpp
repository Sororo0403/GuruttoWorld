#include <Engine/Graphics/Renderers/ParticleRenderer.h>
#include <Engine/Graphics/ShaderCompiler.h>
#include <Engine/Graphics/Resources/DepthBuffer.h>
#include <Engine/Core/Log.h>

#include <Engine/Graphics/Resources/IndexedMeshBuffer.h>
#include <Engine/Graphics/Resources/RenderTargetBinding.h>
#include <cmath>
#include <format>
#include <utility>

namespace
{
    using Microsoft::WRL::ComPtr;

    bool Check(HRESULT result, const char* operation)
    {
        if (FAILED(result))
        {
            Engine::Log::Error(std::format("{} failed: 0x{:08X}", operation, static_cast<unsigned long>(result)));
            return false;
        }
        return true;
    }
}

namespace Engine
{
    bool ParticleRenderer::Initialize(ID3D12Device* device, ID3D12CommandQueue* queue,
        std::shared_ptr<const Texture2D> texture, const std::filesystem::path& shaderPath)
    {
        if (device == nullptr || queue == nullptr || initialized_ || !texture || texture->GetDescriptorHeap() == nullptr)
        {
            return false;
        }
        ComPtr<ID3D12Device> textureDevice;
        ComPtr<ID3D12Device> queueDevice;
        if (queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT ||
            FAILED(texture->GetDescriptorHeap()->GetDevice(IID_PPV_ARGS(&textureDevice))) || textureDevice.Get() != device ||
            FAILED(queue->GetDevice(IID_PPV_ARGS(&queueDevice))) || queueDevice.Get() != device)
        {
            return false;
        }
        if (!CreateRootSignature(device) || !CreatePipelineState(device, shaderPath) ||
            !CreateMeshBuffer(device, queue))
        {
            meshBuffer_.Reset();
            vertexBufferView_ = {};
            indexBufferView_ = {};
            pipelineState_.Reset();
            hdrPipelineState_.Reset();
            rootSignature_.Reset();
            return false;
        }
        texture_ = std::move(texture);
        initialized_ = true;
        Log::Info("Particle renderer initialized.");
        return true;
    }

    bool ParticleRenderer::CreateRootSignature(ID3D12Device* device)
    {
        D3D12_DESCRIPTOR_RANGE textureRange{};
        textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        textureRange.NumDescriptors = 1;
        textureRange.BaseShaderRegister = 0;
        textureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        D3D12_ROOT_PARAMETER parameters[3]{};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[0].Constants.ShaderRegister = 0;
        parameters[0].Constants.Num32BitValues = 20;
        parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[1].DescriptorTable.NumDescriptorRanges = 1;
        parameters[1].DescriptorTable.pDescriptorRanges = &textureRange;
        parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        parameters[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[2].Constants.ShaderRegister=1; parameters[2].Constants.Num32BitValues=1;
        parameters[2].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.MaxAnisotropy = 1;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC description{};
        description.NumParameters = 3;
        description.pParameters = parameters;
        description.NumStaticSamplers = 1;
        description.pStaticSamplers = &sampler;
        description.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> errors;
        const HRESULT result = D3D12SerializeRootSignature(&description, D3D_ROOT_SIGNATURE_VERSION_1,
            &signature, &errors);
        if (errors)
        {
            Log::Error(static_cast<const char*>(errors->GetBufferPointer()));
        }
        if (!Check(result, "Serialize particle root signature"))
        {
            return false;
        }
        return Check(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
            IID_PPV_ARGS(&rootSignature_)), "Create particle root signature");
    }

    bool ParticleRenderer::CreatePipelineState(ID3D12Device* device, const std::filesystem::path& shaderPath)
    {
        ComPtr<ID3DBlob> vertexShader;
        ComPtr<ID3DBlob> pixelShader;
        if (!CompileShader(shaderPath, "VSMain", "vs_5_0", vertexShader) ||
            !CompileShader(shaderPath, "PSMain", "ps_5_0", pixelShader))
        {
            return false;
        }
        const D3D12_INPUT_ELEMENT_DESC elements[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        };
        D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
        description.InputLayout = { elements, 1 };
        description.pRootSignature = rootSignature_.Get();
        description.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
        description.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
        description.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        description.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        description.RasterizerState.DepthClipEnable = TRUE;
        auto& blend = description.BlendState.RenderTarget[0];
        blend.BlendEnable = TRUE;
        blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend.DestBlend = D3D12_BLEND_ONE;
        blend.BlendOp = D3D12_BLEND_OP_ADD;
        blend.SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend.LogicOp = D3D12_LOGIC_OP_NOOP;
        blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        description.DepthStencilState.DepthEnable = TRUE;
        description.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        description.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        description.DepthStencilState.StencilEnable = FALSE;
        description.SampleMask = UINT_MAX;
        description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        description.NumRenderTargets = 1;
        description.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.DSVFormat = DepthBuffer::Format;
        description.SampleDesc.Count = 1;
        if (!Check(device->CreateGraphicsPipelineState(&description, IID_PPV_ARGS(&pipelineState_)),"Create particle pipeline state")) return false;
        description.RTVFormats[0]=DXGI_FORMAT_R16G16B16A16_FLOAT;
        return Check(device->CreateGraphicsPipelineState(&description,IID_PPV_ARGS(&hdrPipelineState_)),"Create HDR particle pipeline");
    }

    bool ParticleRenderer::CreateMeshBuffer(ID3D12Device* device, ID3D12CommandQueue* queue)
    {
        constexpr std::array<std::array<float, 2>, 4> Vertices
        {
            std::array<float, 2>{ 0.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f }, { 1.0f, 1.0f }
        };
        constexpr std::array<std::uint32_t, 6> Indices{ 0, 1, 2, 2, 1, 3 };
        return CreateIndexedMeshBuffer(device, queue, std::as_bytes(std::span(Vertices)), sizeof(Vertices[0]),
            Indices, meshBuffer_, vertexBufferView_, indexBufferView_);
    }

    void ParticleRenderer::Draw(ID3D12GraphicsCommandList* commands, const DirectX::XMFLOAT4X4& worldViewProjection,
        const std::array<float, 4>& color) const
    {
        if (!initialized_ || commands == nullptr) return;
        RenderTargetBinding target;
        const UINT hdr=RenderTargetBinding::Current(commands,target) && target.format==DXGI_FORMAT_R16G16B16A16_FLOAT ? 1U : 0U;
        commands->SetPipelineState(hdr ? hdrPipelineState_.Get() : pipelineState_.Get());
        commands->SetGraphicsRootSignature(rootSignature_.Get());
        // HLSL の行列16要素と色4要素を、それぞれのオフセットへ設定します。
        commands->SetGraphicsRoot32BitConstants(0, 16, &worldViewProjection, 0);
        commands->SetGraphicsRoot32BitConstants(0, 4, color.data(), 16);
        commands->SetGraphicsRoot32BitConstants(2,1,&hdr,0);
        ID3D12DescriptorHeap* heaps[] = { texture_->GetDescriptorHeap() };
        commands->SetDescriptorHeaps(1, heaps);
        commands->SetGraphicsRootDescriptorTable(1, texture_->GetGpuHandle());
        commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commands->IASetVertexBuffers(0, 1, &vertexBufferView_);
        commands->IASetIndexBuffer(&indexBufferView_);
        commands->DrawIndexedInstanced(6, 1, 0, 0, 0);
    }
}
