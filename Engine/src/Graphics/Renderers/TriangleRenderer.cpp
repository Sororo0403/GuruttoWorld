#include <Engine/Graphics/Renderers/TriangleRenderer.h>
#include <Engine/Core/Log.h>
#include <Engine/Graphics/Resources/DepthBuffer.h>

#include <Engine/Graphics/ShaderCompiler.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <Engine/Graphics/Resources/IndexedMeshBuffer.h>
#include <format>

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
    bool TriangleRenderer::Initialize(ID3D12Device* device, ID3D12CommandQueue* queue, const std::filesystem::path& texturePath,
        const std::filesystem::path& shaderPath,
        const std::array<TriangleVertex, 3>& vertices)
    {
        if (device == nullptr || queue == nullptr || initialized_)
        {
            return false;
        }
        if (!CreateRootSignature(device) || !CreatePipelineState(device, shaderPath) ||
            !CreateMeshBuffer(device, queue, vertices) || !texture_.Initialize(device, queue, texturePath))
        {
            meshBuffer_.Reset();
            vertexBufferView_ = {};
            indexBufferView_ = {};
            pipelineState_.Reset();
            rootSignature_.Reset();
            return false;
        }
        initialized_ = true;
        Log::Info("Triangle renderer initialized.");
        return true;
    }

    bool TriangleRenderer::CreateRootSignature(ID3D12Device* device)
    {
        D3D12_DESCRIPTOR_RANGE textureRange{};
        textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        textureRange.NumDescriptors = 1;
        textureRange.BaseShaderRegister = 0;
        textureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        D3D12_ROOT_PARAMETER parameters[3]{};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[0].Constants.ShaderRegister = 0;
        parameters[0].Constants.Num32BitValues = 12;
        parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[1].DescriptorTable.NumDescriptorRanges = 1;
        parameters[1].DescriptorTable.pDescriptorRanges = &textureRange;
        parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[2].Constants.ShaderRegister = 2;
        parameters[2].Constants.Num32BitValues = 8;
        parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
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
        if (!Check(result, "Serialize triangle root signature"))
        {
            return false;
        }
        return Check(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
            IID_PPV_ARGS(&rootSignature_)), "Create triangle root signature");
    }

    bool TriangleRenderer::CreatePipelineState(ID3D12Device* device, const std::filesystem::path& shaderPath)
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
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(TriangleVertex, position)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(TriangleVertex, color)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(TriangleVertex, uv)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        };
        D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
        description.pRootSignature = rootSignature_.Get();
        description.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
        description.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
        description.InputLayout = { elements, 3 };
        description.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        description.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        description.RasterizerState.DepthClipEnable = TRUE;
        auto& blend = description.BlendState.RenderTarget[0];
        blend.SrcBlend = D3D12_BLEND_ONE;
        blend.DestBlend = D3D12_BLEND_ZERO;
        blend.BlendOp = D3D12_BLEND_OP_ADD;
        blend.SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.DestBlendAlpha = D3D12_BLEND_ZERO;
        blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend.LogicOp = D3D12_LOGIC_OP_NOOP;
        blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        description.DepthStencilState.DepthEnable = TRUE;
        description.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        description.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        description.DepthStencilState.StencilEnable = FALSE;
        description.SampleMask = UINT_MAX;
        description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        description.NumRenderTargets = 1;
        description.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.DSVFormat = DepthBuffer::Format;
        description.SampleDesc.Count = 1;
        return Check(device->CreateGraphicsPipelineState(&description, IID_PPV_ARGS(&pipelineState_)),
            "Create triangle pipeline state");
    }

    bool TriangleRenderer::CreateMeshBuffer(ID3D12Device* device, ID3D12CommandQueue* queue,
        const std::array<TriangleVertex, 3>& vertices)
    {
        constexpr std::array<std::uint32_t, 3> Indices{ 0, 1, 2 };
        return CreateIndexedMeshBuffer(device, queue, std::as_bytes(std::span(vertices)), sizeof(TriangleVertex),
            Indices, meshBuffer_, vertexBufferView_, indexBufferView_);
    }

    void TriangleRenderer::Draw(ID3D12GraphicsCommandList* commands, float aspectRatio, float rotationY,
        const std::array<float, 3>& translation, const std::array<float, 4>& tint, const UvTransform& uvTransform) const
    {
        if (!initialized_ || commands == nullptr || aspectRatio <= 0.0f)
        {
            return;
        }
        const std::array<float, 12> constants
        {
            std::min(1.0f, 1.0f / aspectRatio), std::min(1.0f, aspectRatio),
            std::cos(rotationY), std::sin(rotationY),
            translation[0], translation[1], translation[2], 0.0f,
            tint[0], tint[1], tint[2], tint[3]
        };
        commands->SetPipelineState(pipelineState_.Get());
        commands->SetGraphicsRootSignature(rootSignature_.Get());
        commands->SetGraphicsRoot32BitConstants(0, static_cast<UINT>(constants.size()), constants.data(), 0);
        const auto uvConstants = uvTransform.GetConstants();
        commands->SetGraphicsRoot32BitConstants(2, 8, uvConstants.data(), 0);
        ID3D12DescriptorHeap* heaps[] = { texture_.GetDescriptorHeap() };
        commands->SetDescriptorHeaps(1, heaps);
        commands->SetGraphicsRootDescriptorTable(1, texture_.GetGpuHandle());
        commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commands->IASetVertexBuffers(0, 1, &vertexBufferView_);
        commands->IASetIndexBuffer(&indexBufferView_);
        commands->DrawIndexedInstanced(3, 1, 0, 0, 0);
    }
}
