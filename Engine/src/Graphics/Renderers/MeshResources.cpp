#include <Engine/Graphics/Renderers/MeshResources.h>
#include <Engine/Graphics/ShaderCompiler.h>
#include <Engine/Graphics/Resources/DepthBuffer.h>
#include <Engine/Graphics/Resources/IndexedMeshBuffer.h>
#include <Engine/Core/Log.h>

#include <algorithm>
#include <cstddef>
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
    bool MeshResources::Initialize(ID3D12Device* device, const std::filesystem::path& shaderPath)
    {
        if (device == nullptr || device_)
        {
            return false;
        }
        if (!CreateRootSignature(device) || !CreatePipelineState(device, shaderPath))
        {
            pipelineState_.Reset();
            mirroredPipelineState_.Reset();
            rootSignature_.Reset();
            return false;
        }
        device_ = device;
        return true;
    }

    std::shared_ptr<Texture2D> MeshResources::GetTexture(ID3D12Device* device,
        ID3D12CommandQueue* queue, const std::filesystem::path& path)
    {
        if (device == nullptr || device != device_.Get() || queue == nullptr)
        {
            return {};
        }
        std::error_code error;
        const auto key = path.empty() ? std::filesystem::path{} : std::filesystem::weakly_canonical(path, error);
        if (error)
        {
            Log::Error("Cannot resolve the texture cache path.");
            return {};
        }
        if (const auto found = textures_.find(key); found != textures_.end())
        {
            return found->second;
        }
        auto texture = std::make_shared<Texture2D>();
        if (!texture->Initialize(device, queue, key))
        {
            return {};
        }
        textures_.emplace(key, texture);
        return texture;
    }

    ID3D12RootSignature* MeshResources::GetRootSignature() const noexcept
    {
        return rootSignature_.Get();
    }

    ID3D12PipelineState* MeshResources::GetPipelineState(bool mirrored,bool transparent) const noexcept
    {
        if (transparent) return mirrored ? transparentMirroredPipelineState_.Get() : transparentPipelineState_.Get();
        return mirrored ? mirroredPipelineState_.Get() : pipelineState_.Get();
    }

    bool MeshResources::PathLess::operator()(const std::filesystem::path& left, const std::filesystem::path& right) const
    {
        return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    }

    bool MeshResources::CreateRootSignature(ID3D12Device* device)
    {
        D3D12_DESCRIPTOR_RANGE textureRange{};
        textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        textureRange.NumDescriptors = 4;
        textureRange.BaseShaderRegister = 0;
        textureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        D3D12_ROOT_PARAMETER parameters[4]{};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[0].Constants.ShaderRegister = 0;
        parameters[0].Constants.Num32BitValues = 32;
        parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[1].DescriptorTable.NumDescriptorRanges = 1;
        parameters[1].DescriptorTable.pDescriptorRanges = &textureRange;
        parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[2].Constants.ShaderRegister = 1;
        parameters[2].Constants.Num32BitValues = 26;
        parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[3].Constants.ShaderRegister = 2;
        parameters[3].Constants.Num32BitValues = 5;
        parameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
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
        std::array<D3D12_STATIC_SAMPLER_DESC,2> samplers{sampler,sampler};
        auto& comparison=samplers[1];
        comparison.ShaderRegister=1;
        comparison.Filter=D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
        comparison.AddressU=comparison.AddressV=comparison.AddressW=D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        comparison.BorderColor=D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
        comparison.ComparisonFunc=D3D12_COMPARISON_FUNC_LESS_EQUAL;
        D3D12_ROOT_SIGNATURE_DESC description{};
        description.NumParameters = 4;
        description.pParameters = parameters;
        description.NumStaticSamplers = 2;
        description.pStaticSamplers = samplers.data();
        description.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> errors;
        const HRESULT result = D3D12SerializeRootSignature(&description, D3D_ROOT_SIGNATURE_VERSION_1,
            &signature, &errors);
        if (errors)
        {
            Log::Error(static_cast<const char*>(errors->GetBufferPointer()));
        }
        if (!Check(result, "Serialize mesh root signature"))
        {
            return false;
        }
        return Check(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
            IID_PPV_ARGS(&rootSignature_)), "Create mesh root signature");
    }

    bool MeshResources::CreatePipelineState(ID3D12Device* device, const std::filesystem::path& shaderPath)
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
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(MeshVertex, color)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(MeshVertex, position)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(MeshVertex, normal)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(MeshVertex, uv)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        };
        D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
        description.pRootSignature = rootSignature_.Get();
        description.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
        description.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
        description.InputLayout = { elements, 4 };
        description.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        description.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
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
        if (!Check(device->CreateGraphicsPipelineState(&description, IID_PPV_ARGS(&pipelineState_)),
            "Create mesh pipeline state")) return false;
        description.RasterizerState.FrontCounterClockwise = TRUE;
        if (!Check(device->CreateGraphicsPipelineState(&description, IID_PPV_ARGS(&mirroredPipelineState_)),"Create mirrored mesh pipeline state")) return false;
        blend.BlendEnable=TRUE; blend.SrcBlend=D3D12_BLEND_SRC_ALPHA; blend.DestBlend=D3D12_BLEND_INV_SRC_ALPHA;
        blend.DestBlendAlpha=D3D12_BLEND_INV_SRC_ALPHA;
        description.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;
        if (!Check(device->CreateGraphicsPipelineState(&description,IID_PPV_ARGS(&transparentMirroredPipelineState_)),"Create transparent mirrored mesh pipeline")) return false;
        description.RasterizerState.FrontCounterClockwise=FALSE;
        return Check(device->CreateGraphicsPipelineState(&description,IID_PPV_ARGS(&transparentPipelineState_)),"Create transparent mesh pipeline");
    }

}
