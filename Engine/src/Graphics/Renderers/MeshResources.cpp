#include <Engine/Graphics/Renderers/MeshResources.h>
#include <Engine/Graphics/ShaderCompiler.h>
#include <Engine/Graphics/Resources/DepthBuffer.h>
#include <Engine/Graphics/Resources/IndexedMeshBuffer.h>
#include <Engine/Core/Log.h>

#include <algorithm>
#include <cstddef>
#include <format>
#include <Engine/Core/Profiler.h>
#include <mutex>
#include <tuple>

#pragma comment(lib, "d3d12.lib")

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
    struct MeshResources::PipelineCache
    {
        using Key = std::tuple<ID3D12Device*, ID3D12CommandQueue*, std::string>;
        struct Entry
        {
            std::shared_ptr<PipelineBundle> pipeline;
            uint64_t used = 0;
        };
        std::mutex mutex;
        std::map<Key, Entry> entries;
        uint64_t sequence = 0;
    };
    MeshResources::PipelineCache& MeshResources::SharedPipelines()
    {
        static PipelineCache cache;
        return cache;
    }
    void MeshResources::ClearPipelineCache(ID3D12Device* device)
    {
        auto& cache = SharedPipelines();
        std::lock_guard lock(cache.mutex);
        std::erase_if(cache.entries,
                      [device](const auto& entry)
                      {
                          return std::get<0>(entry.first) == device;
                      });
    }
    bool MeshResources::Initialize(ID3D12Device* device, const std::filesystem::path& shaderPath,
                                   ID3D12CommandQueue* queue)
    {
        if (device == nullptr || device_)
        {
            return false;
        }
        std::string revision;
        if (!GetShaderRevision(shaderPath, revision))
            return false;
        auto& cache = SharedPipelines();
        std::lock_guard lock(cache.mutex);
        PipelineCache::Key key{device, queue, std::move(revision)};
        if (const auto found = cache.entries.find(key); found != cache.entries.end())
        {
            pipeline_ = found->second.pipeline;
            found->second.used = ++cache.sequence;
            CpuProfiler::Add("Mesh pipeline cache hit", 0);
        }
        if (!pipeline_)
        {
            pipeline_ = std::make_shared<PipelineBundle>();
            CpuScope scope("Mesh pipeline creation");
            if (!CreateRootSignature(device) || !CreatePipelineState(device, shaderPath))
            {
                pipeline_.reset();
                return false;
            }
            std::string currentRevision;
            if (!GetShaderRevision(shaderPath, currentRevision) || currentRevision != std::get<2>(key))
            {
                pipeline_.reset();
                return false;
            }
            // シーン破棄後も再利用し、シェーダー変更や多数のデバイスで無制限に増やしません。
            while (cache.entries.size() >= 64)
            {
                const auto oldest = std::min_element(cache.entries.begin(), cache.entries.end(),
                                                     [](const auto& a, const auto& b)
                                                     {
                                                         return a.second.used < b.second.used;
                                                     });
                cache.entries.erase(oldest);
            }
            cache.entries.emplace(std::move(key), PipelineCache::Entry{pipeline_, ++cache.sequence});
        }
        device_ = device;
        return true;
    }

    std::shared_ptr<Texture2D> MeshResources::GetTexture(ID3D12Device* device, ID3D12CommandQueue* queue,
                                                         const std::filesystem::path& path)
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
        const auto stamp = AssetSourceStamp::Capture(key);
        if (!stamp)
            return {};
        if (const auto found = textures_.find(key); found != textures_.end() && textureStamps_.at(key) == *stamp)
        {
            return found->second;
        }
        auto texture = std::make_shared<Texture2D>();
        if (!texture->Initialize(device, queue, key))
        {
            return {};
        }
        if (AssetSourceStamp::Capture(key) != stamp)
            return {};
        textures_[key] = texture;
        textureStamps_[key] = *stamp;
        return texture;
    }

    ID3D12RootSignature* MeshResources::GetRootSignature() const noexcept
    {
        return pipeline_ ? pipeline_->rootSignature_.Get() : nullptr;
    }

    ID3D12PipelineState* MeshResources::GetPipelineState(bool mirrored, bool transparent, bool hdr) const noexcept
    {
        if (!pipeline_)
            return nullptr;
        if (hdr)
            return pipeline_->hdrPipelineStates_[(transparent ? 2 : 0) + (mirrored ? 1 : 0)].Get();
        if (transparent)
            return mirrored ? pipeline_->transparentMirroredPipelineState_.Get()
                            : pipeline_->transparentPipelineState_.Get();
        return mirrored ? pipeline_->mirroredPipelineState_.Get() : pipeline_->pipelineState_.Get();
    }

    bool MeshResources::PathLess::operator()(const std::filesystem::path& left,
                                             const std::filesystem::path& right) const
    {
        return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    }

    bool MeshResources::CreateRootSignature(ID3D12Device* device)
    {
        D3D12_DESCRIPTOR_RANGE textureRange{};
        textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        textureRange.NumDescriptors = 9;
        textureRange.BaseShaderRegister = 0;
        textureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        D3D12_ROOT_PARAMETER parameters[5]{};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[0].Constants.ShaderRegister = 0;
        parameters[0].Constants.Num32BitValues = 32;
        parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[1].DescriptorTable.NumDescriptorRanges = 1;
        parameters[1].DescriptorTable.pDescriptorRanges = &textureRange;
        parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        parameters[2].Descriptor.ShaderRegister = 1;
        parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[3].Constants.ShaderRegister = 2;
        parameters[3].Constants.Num32BitValues = 5;
        parameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        parameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[4].Constants.ShaderRegister = 5;
        parameters[4].Constants.Num32BitValues = 4;
        parameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
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
        std::array<D3D12_STATIC_SAMPLER_DESC, 3> samplers{sampler, sampler, sampler};
        auto& comparison = samplers[1];
        comparison.ShaderRegister = 1;
        comparison.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
        comparison.AddressU = comparison.AddressV = comparison.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        comparison.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
        comparison.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        samplers[2].ShaderRegister = 2;
        samplers[2].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        D3D12_ROOT_SIGNATURE_DESC description{};
        description.NumParameters = 5;
        description.pParameters = parameters;
        description.NumStaticSamplers = 3;
        description.pStaticSamplers = samplers.data();
        description.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> errors;
        const HRESULT result =
            D3D12SerializeRootSignature(&description, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors);
        if (errors)
        {
            Log::Error(static_cast<const char*>(errors->GetBufferPointer()));
        }
        if (!Check(result, "Serialize mesh root signature"))
        {
            return false;
        }
        return Check(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                                 IID_PPV_ARGS(&pipeline_->rootSignature_)),
                     "Create mesh root signature");
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
        const D3D12_INPUT_ELEMENT_DESC elements[] = {
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(MeshVertex, color)),
             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(MeshVertex, position)),
             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(MeshVertex, normal)),
             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(MeshVertex, uv)),
             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, static_cast<UINT>(offsetof(MeshVertex, joints)),
             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(MeshVertex, weights)),
             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
        D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
        description.pRootSignature = pipeline_->rootSignature_.Get();
        description.VS = {vertexShader->GetBufferPointer(), vertexShader->GetBufferSize()};
        description.PS = {pixelShader->GetBufferPointer(), pixelShader->GetBufferSize()};
        description.InputLayout = {elements, 6};
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
        if (!Check(device->CreateGraphicsPipelineState(&description, IID_PPV_ARGS(&pipeline_->pipelineState_)),
                   "Create mesh pipeline state"))
            return false;
        description.RasterizerState.FrontCounterClockwise = TRUE;
        if (!Check(device->CreateGraphicsPipelineState(&description, IID_PPV_ARGS(&pipeline_->mirroredPipelineState_)),
                   "Create mirrored mesh pipeline state"))
            return false;
        blend.BlendEnable = TRUE;
        blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        description.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        if (!Check(device->CreateGraphicsPipelineState(&description,
                                                       IID_PPV_ARGS(&pipeline_->transparentMirroredPipelineState_)),
                   "Create transparent mirrored mesh pipeline"))
            return false;
        description.RasterizerState.FrontCounterClockwise = FALSE;
        if (!Check(
                device->CreateGraphicsPipelineState(&description, IID_PPV_ARGS(&pipeline_->transparentPipelineState_)),
                "Create transparent mesh pipeline"))
            return false;
        description.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        for (size_t index = 0; index < pipeline_->hdrPipelineStates_.size(); ++index)
        {
            const bool transparent = index >= 2;
            description.RasterizerState.FrontCounterClockwise = (index % 2) != 0;
            blend.BlendEnable = transparent ? TRUE : FALSE;
            description.DepthStencilState.DepthWriteMask =
                transparent ? D3D12_DEPTH_WRITE_MASK_ZERO : D3D12_DEPTH_WRITE_MASK_ALL;
            if (!Check(device->CreateGraphicsPipelineState(&description,
                                                           IID_PPV_ARGS(&pipeline_->hdrPipelineStates_[index])),
                       "Create HDR mesh pipeline"))
                return false;
        }
        return true;
    }

}
