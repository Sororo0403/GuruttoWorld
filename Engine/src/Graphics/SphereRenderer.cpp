#include <Engine/Graphics/SphereRenderer.h>
#include <Engine/Graphics/ShaderCompiler.h>
#include <Engine/Graphics/DepthBuffer.h>
#include <Engine/Core/Log.h>

#include <cmath>
#include <cstddef>
#include <cstring>
#include <format>
#include <memory>
#include <numbers>

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
    bool SphereRenderer::Initialize(ID3D12Device* device, ID3D12CommandQueue* queue,
        const std::filesystem::path& texturePath, const std::filesystem::path& shaderPath)
    {
        if (initialized_ || device == nullptr || queue == nullptr || queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT)
        {
            return false;
        }
        if (!CreateRootSignature(device) || !CreatePipelineState(device, shaderPath) ||
            !CreateMeshBuffer(device, queue) || !texture_.Initialize(device, queue, texturePath))
        {
            meshBuffer_.Reset();
            pipelineState_.Reset();
            rootSignature_.Reset();
            indexCount_ = 0;
            vertexView_ = {};
            indexView_ = {};
            return false;
        }
        initialized_ = true;
        Log::Info(std::format("Sphere renderer initialized: {} triangles.", indexCount_ / 3));
        return true;
    }

    void SphereRenderer::GenerateMesh(std::vector<Vertex>& vertices, std::vector<std::uint32_t>& indices)
    {
        constexpr UINT latitudeCount = 32;
        constexpr UINT longitudeCount = 64;
        vertices.reserve((latitudeCount + 1) * (longitudeCount + 1));
        indices.reserve(6 * longitudeCount * (latitudeCount - 1));
        for (UINT latitude = 0; latitude <= latitudeCount; ++latitude)
        {
            const float v = static_cast<float>(latitude) / latitudeCount;
            const float theta = v * std::numbers::pi_v<float>;
            // 極と継ぎ目を正確に一致させ、浮動小数点の誤差による隙間を防ぎます。
            const float radius = latitude == 0 || latitude == latitudeCount ? 0.0f : std::sin(theta);
            for (UINT longitude = 0; longitude <= longitudeCount; ++longitude)
            {
                const float u = static_cast<float>(longitude) / longitudeCount;
                const float phi = longitude == longitudeCount ? 0.0f : u * 2.0f * std::numbers::pi_v<float>;
                const std::array<float, 3> position{ radius * std::cos(phi), std::cos(theta), radius * std::sin(phi) };
                vertices.push_back({ position, position, { u, v } });
            }
        }
        for (UINT latitude = 0; latitude < latitudeCount; ++latitude)
        {
            for (UINT longitude = 0; longitude < longitudeCount; ++longitude)
            {
                const UINT a = latitude * (longitudeCount + 1) + longitude;
                const UINT b = a + longitudeCount + 1;
                if (latitude != 0)
                {
                    indices.insert(indices.end(), { a, a + 1, b });
                }
                if (latitude + 1 != latitudeCount)
                {
                    indices.insert(indices.end(), { a + 1, b + 1, b });
                }
            }
        }
    }

    bool SphereRenderer::CreateMeshBuffer(ID3D12Device* device, ID3D12CommandQueue* queue)
    {
        std::vector<Vertex> vertices;
        std::vector<std::uint32_t> indices;
        GenerateMesh(vertices, indices);
        const UINT vertexBytes = static_cast<UINT>(vertices.size() * sizeof(Vertex));
        const UINT indexBytes = static_cast<UINT>(indices.size() * sizeof(std::uint32_t));
        const UINT totalBytes = vertexBytes + indexBytes;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap.CreationNodeMask = heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC description{};
        description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        description.Width = totalBytes;
        description.Height = 1;
        description.DepthOrArraySize = 1;
        description.MipLevels = 1;
        description.SampleDesc.Count = 1;
        description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (!Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&meshBuffer_)), "Create sphere mesh buffer"))
        {
            return false;
        }
        heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        ComPtr<ID3D12Resource> upload;
        if (!Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)), "Create sphere upload buffer"))
        {
            return false;
        }
        void* mapped = nullptr;
        const D3D12_RANGE readRange{ 0, 0 };
        if (!Check(upload->Map(0, &readRange, &mapped), "Map sphere mesh"))
        {
            return false;
        }
        std::memcpy(mapped, vertices.data(), vertexBytes);
        std::memcpy(static_cast<unsigned char*>(mapped) + vertexBytes, indices.data(), indexBytes);
        const D3D12_RANGE writtenRange{ 0, totalBytes };
        upload->Unmap(0, &writtenRange);
        if (!UploadMesh(device, queue, upload.Get()))
        {
            return false;
        }
        vertexView_ = { meshBuffer_->GetGPUVirtualAddress(), vertexBytes, static_cast<UINT>(sizeof(Vertex)) };
        indexView_ = { meshBuffer_->GetGPUVirtualAddress() + vertexBytes, indexBytes, DXGI_FORMAT_R32_UINT };
        indexCount_ = static_cast<UINT>(indices.size());
        return true;
    }

    bool SphereRenderer::UploadMesh(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12Resource* upload)
    {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> commands;
        ComPtr<ID3D12Fence> fence;
        std::unique_ptr<void, decltype(&CloseHandle)> event(CreateEventW(nullptr, FALSE, FALSE, nullptr), CloseHandle);
        if (!event)
        {
            Log::Error("Create sphere upload event failed.");
            return false;
        }
        if (!Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)),
                "Create sphere upload allocator") ||
            !Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                IID_PPV_ARGS(&commands)), "Create sphere upload commands") ||
            !Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "Create sphere upload fence") ||
            !Check(fence->SetEventOnCompletion(1, event.get()), "Set sphere upload completion event"))
        {
            return false;
        }
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = meshBuffer_.Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        commands->ResourceBarrier(1, &barrier);
        commands->CopyBufferRegion(meshBuffer_.Get(), 0, upload, 0, meshBuffer_->GetDesc().Width);
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER;
        commands->ResourceBarrier(1, &barrier);
        if (!Check(commands->Close(), "Close sphere upload commands"))
        {
            return false;
        }
        ID3D12CommandList* lists[] = { commands.Get() };
        queue->ExecuteCommandLists(1, lists);
        if (!Check(queue->Signal(fence.Get(), 1), "Signal sphere upload fence"))
        {
            return false;
        }
        if (WaitForSingleObject(event.get(), INFINITE) != WAIT_OBJECT_0)
        {
            Log::Error("Wait for sphere upload failed.");
            return false;
        }
        return Check(device->GetDeviceRemovedReason(), "Sphere upload device status");
    }

    bool SphereRenderer::CreateRootSignature(ID3D12Device* device)
    {
        D3D12_DESCRIPTOR_RANGE textureRange{};
        textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        textureRange.NumDescriptors = 1;
        textureRange.BaseShaderRegister = 0;
        textureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        D3D12_ROOT_PARAMETER parameters[2]{};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[0].Constants.ShaderRegister = 0;
        parameters[0].Constants.Num32BitValues = 32;
        parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[1].DescriptorTable.NumDescriptorRanges = 1;
        parameters[1].DescriptorTable.pDescriptorRanges = &textureRange;
        parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
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
        description.NumParameters = 2;
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
        if (!Check(result, "Serialize sphere root signature"))
        {
            return false;
        }
        return Check(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
            IID_PPV_ARGS(&rootSignature_)), "Create sphere root signature");
    }

    bool SphereRenderer::CreatePipelineState(ID3D12Device* device, const std::filesystem::path& shaderPath)
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
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(Vertex, position)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(Vertex, normal)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(Vertex, uv)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        };
        D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
        description.pRootSignature = rootSignature_.Get();
        description.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
        description.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
        description.InputLayout = { elements, 3 };
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
        description.DSVFormat = DepthBuffer::format;
        description.SampleDesc.Count = 1;
        return Check(device->CreateGraphicsPipelineState(&description, IID_PPV_ARGS(&pipelineState_)),
            "Create sphere pipeline state");
    }

    void SphereRenderer::Draw(ID3D12GraphicsCommandList* commands, const DirectX::XMFLOAT4X4& world,
        const DirectX::XMFLOAT4X4& viewProjection) const
    {
        if (!initialized_ || commands == nullptr)
        {
            return;
        }
        using namespace DirectX;
        const XMMATRIX worldMatrix = XMLoadFloat4x4(&world);
        std::array<XMFLOAT4X4, 2> constants;
        XMStoreFloat4x4(&constants[0], XMMatrixTranspose(XMMatrixInverse(nullptr, worldMatrix)));
        XMStoreFloat4x4(&constants[1], worldMatrix * XMLoadFloat4x4(&viewProjection));
        static_assert(sizeof(constants) == sizeof(float) * 32);
        commands->SetPipelineState(pipelineState_.Get());
        commands->SetGraphicsRootSignature(rootSignature_.Get());
        commands->SetGraphicsRoot32BitConstants(0, 32, constants.data(), 0);
        ID3D12DescriptorHeap* heaps[] = { texture_.GetDescriptorHeap() };
        commands->SetDescriptorHeaps(1, heaps);
        commands->SetGraphicsRootDescriptorTable(1, texture_.GetGpuHandle());
        commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commands->IASetVertexBuffers(0, 1, &vertexView_);
        commands->IASetIndexBuffer(&indexView_);
        commands->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);
    }
}
