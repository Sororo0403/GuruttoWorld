#include <Engine/Graphics/Renderers/MeshRenderer.h>
#include <Engine/Graphics/Resources/IndexedMeshBuffer.h>
#include <Engine/Core/Log.h>

#include <algorithm>
#include <utility>
#include <format>

namespace Engine
{
    ID3D12DescriptorHeap* MeshRenderer::Bindings(const ShadowMap* shadow) const
    {
        auto* key=shadow ? shadow->Resource() : nullptr;
        if (const auto found=bindings_.find(key);found!=bindings_.end()) return found->second.Get();
        Microsoft::WRL::ComPtr<ID3D12Device> device;
        if (FAILED(meshBuffer_->GetDevice(IID_PPV_ARGS(&device)))) return nullptr;
        D3D12_DESCRIPTOR_HEAP_DESC description{};
        description.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        description.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE; description.NumDescriptors=2;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
        if (FAILED(device->CreateDescriptorHeap(&description,IID_PPV_ARGS(&heap)))) return nullptr;
        auto handle=heap->GetCPUDescriptorHandleForHeapStart();
        device->CopyDescriptorsSimple(1,handle,texture_->GetShaderResourceView(),description.Type);
        handle.ptr+=device->GetDescriptorHandleIncrementSize(description.Type);
        if (shadow) device->CopyDescriptorsSimple(1,handle,shadow->View(),description.Type);
        else {
            D3D12_SHADER_RESOURCE_VIEW_DESC view{};
            view.Format=DXGI_FORMAT_R32_FLOAT; view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
            view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; view.Texture2D.MipLevels=1;
            device->CreateShaderResourceView(nullptr,&view,handle);
        }
        // Immutable pairs are never rewritten while previous frames are using them.
        return bindings_.emplace(key,std::move(heap)).first->second.Get();
    }
    void MeshRenderer::DrawShadow(ID3D12GraphicsCommandList* commands,const DirectX::XMFLOAT4X4& world,const ShadowMap& shadow) const
    {
        if (initialized_ && commands) shadow.Draw(commands,world,vertexView_,indexView_,indexCount_);
    }
    bool MeshRenderer::Initialize(ID3D12Device* device, ID3D12CommandQueue* queue,
        const MeshData& mesh, const std::filesystem::path& shaderPath)
    {
        if (initialized_ || device == nullptr || queue == nullptr)
        {
            return false;
        }
        auto resources = std::make_shared<MeshResources>();
        return resources->Initialize(device, shaderPath) && Initialize(device, queue, mesh, resources);
    }

    bool MeshRenderer::Initialize(ID3D12Device* device, ID3D12CommandQueue* queue,
        const MeshData& mesh, const std::shared_ptr<MeshResources>& resources)
    {
        if (initialized_ || device == nullptr || queue == nullptr || !resources ||
            resources->GetPipelineState() == nullptr || queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT)
        {
            return false;
        }
        auto texture = resources->GetTexture(device, queue, mesh.texturePath);
        if (!texture || !CreateIndexedMeshBuffer(device, queue, std::as_bytes(std::span(mesh.vertices)), sizeof(MeshVertex),
            mesh.indices, meshBuffer_, vertexView_, indexView_))
        {
            return false;
        }
        resources_ = resources;
        texture_ = std::move(texture);
        indexCount_ = static_cast<UINT>(mesh.indices.size());
        initialized_ = true;
        Log::Info(std::format("Mesh renderer initialized: {} triangles.", indexCount_ / 3));
        return true;
    }

    void MeshRenderer::Draw(ID3D12GraphicsCommandList* commands, const DirectX::XMFLOAT4X4& world,
        const DirectX::XMFLOAT4X4& viewProjection, const DirectionalLight& light,
        const std::array<float, 3>& cameraPosition, const UvTransform& uvTransform) const
    {
        if (!initialized_ || commands == nullptr)
        {
            return;
        }
        using namespace DirectX;
        const XMMATRIX worldMatrix = XMLoadFloat4x4(&world);
        // Normal cofactors are derived from the world rows in the vertex shader.
        struct TransformConstants
        {
            XMFLOAT4X4 worldViewProjection;
            XMFLOAT4 worldRows[3];
        } constants;
        const bool mirrored=XMVectorGetX(XMMatrixDeterminant(worldMatrix))<0;
        XMStoreFloat4x4(&constants.worldViewProjection, worldMatrix * XMLoadFloat4x4(&viewProjection));
        const XMMATRIX transposedWorld = XMMatrixTranspose(worldMatrix);
        for (size_t row = 0; row < 3; ++row)
        {
            XMStoreFloat4(&constants.worldRows[row], transposedWorld.r[row]);
        }
        static_assert(sizeof(constants) == sizeof(float) * 28);
        std::array<float, 26> lightConstants
        {
            light.direction[0], light.direction[1], light.direction[2], std::max(0.0f, light.intensity),
            light.color[0], light.color[1], light.color[2], std::max(0.0f, light.ambientIntensity),
            cameraPosition[0], cameraPosition[1], cameraPosition[2], std::max(1.0f, light.shininess),
            std::max(0.0f, light.specularStrength), light.enabled ? 1.0f : 0.0f,
            std::max(0.0f,light.fog.start), std::max(light.fog.start+0.001f,light.fog.end),
            light.fog.color[0],light.fog.color[1],light.fog.color[2],
            light.fog.enabled ? std::clamp(light.fog.strength,0.0f,1.0f) : 0.0f
        };
        if (light.shadow) std::copy(light.shadow->Constants().begin(),light.shadow->Constants().end(),lightConstants.begin()+20);
        auto* heap=Bindings(light.shadow);
        if (!heap) { Log::Error("Cannot allocate mesh texture/shadow bindings."); return; }
        // Transform 28 + lighting/fog/shadow 26 + UV 8 + SRV table 1 = 63 DWORD (limit 64).
        commands->SetPipelineState(resources_->GetPipelineState(mirrored));
        commands->SetGraphicsRootSignature(resources_->GetRootSignature());
        commands->SetGraphicsRoot32BitConstants(0, 28, &constants, 0);
        commands->SetGraphicsRoot32BitConstants(2, 26, lightConstants.data(), 0);
        const auto uvConstants = uvTransform.GetConstants();
        commands->SetGraphicsRoot32BitConstants(3, 8, uvConstants.data(), 0);
        ID3D12DescriptorHeap* heaps[] = { heap };
        commands->SetDescriptorHeaps(1, heaps);
        commands->SetGraphicsRootDescriptorTable(1, heap->GetGPUDescriptorHandleForHeapStart());
        commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commands->IASetVertexBuffers(0, 1, &vertexView_);
        commands->IASetIndexBuffer(&indexView_);
        commands->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);
    }
}
