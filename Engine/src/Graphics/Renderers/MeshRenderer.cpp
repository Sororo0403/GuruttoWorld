#include <Engine/Graphics/Renderers/MeshRenderer.h>
#include <Engine/Graphics/Resources/IndexedMeshBuffer.h>
#include <Engine/Core/Log.h>

#include <algorithm>
#include <utility>
#include <format>

namespace Engine
{
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
        const std::array<float, 20> lightConstants
        {
            light.direction[0], light.direction[1], light.direction[2], std::max(0.0f, light.intensity),
            light.color[0], light.color[1], light.color[2], std::max(0.0f, light.ambientIntensity),
            cameraPosition[0], cameraPosition[1], cameraPosition[2], std::max(1.0f, light.shininess),
            std::max(0.0f, light.specularStrength), light.enabled ? 1.0f : 0.0f,
            std::max(0.0f,light.fog.start), std::max(light.fog.start+0.001f,light.fog.end),
            light.fog.color[0],light.fog.color[1],light.fog.color[2],
            light.fog.enabled ? std::clamp(light.fog.strength,0.0f,1.0f) : 0.0f
        };
        // Transform 28 + lighting/fog 20 + UV 8 + SRV 1 = 57 DWORD (limit 64).
        commands->SetPipelineState(resources_->GetPipelineState(mirrored));
        commands->SetGraphicsRootSignature(resources_->GetRootSignature());
        commands->SetGraphicsRoot32BitConstants(0, 28, &constants, 0);
        commands->SetGraphicsRoot32BitConstants(2, 20, lightConstants.data(), 0);
        const auto uvConstants = uvTransform.GetConstants();
        commands->SetGraphicsRoot32BitConstants(3, 8, uvConstants.data(), 0);
        ID3D12DescriptorHeap* heaps[] = { texture_->GetDescriptorHeap() };
        commands->SetDescriptorHeaps(1, heaps);
        commands->SetGraphicsRootDescriptorTable(1, texture_->GetGpuHandle());
        commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commands->IASetVertexBuffers(0, 1, &vertexView_);
        commands->IASetIndexBuffer(&indexView_);
        commands->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);
    }
}
