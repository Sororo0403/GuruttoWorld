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
        // HLSL の row_major float3x3 は、各行が 16 バイト境界に配置されます。
        struct TransformConstants
        {
            XMFLOAT4 normalRows[3];
            XMFLOAT4X4 worldViewProjection;
            XMFLOAT4 worldRows[3];
        } constants;
        const XMMATRIX normalMatrix = XMMatrixTranspose(XMMatrixInverse(nullptr, worldMatrix));
        for (size_t row = 0; row < 3; ++row)
        {
            XMStoreFloat4(&constants.normalRows[row], normalMatrix.r[row]);
        }
        XMStoreFloat4x4(&constants.worldViewProjection, worldMatrix * XMLoadFloat4x4(&viewProjection));
        const XMMATRIX transposedWorld = XMMatrixTranspose(worldMatrix);
        for (size_t row = 0; row < 3; ++row)
        {
            XMStoreFloat4(&constants.worldRows[row], transposedWorld.r[row]);
        }
        static_assert(sizeof(constants) == sizeof(float) * 40);
        const std::array<float, 14> lightConstants
        {
            light.direction[0], light.direction[1], light.direction[2], std::max(0.0f, light.intensity),
            light.color[0], light.color[1], light.color[2], std::max(0.0f, light.ambientIntensity),
            cameraPosition[0], cameraPosition[1], cameraPosition[2], std::max(1.0f, light.shininess),
            std::max(0.0f, light.specularStrength), light.enabled ? 1.0f : 0.0f
        };
        // 行列 40 + 光源 14 + UV 8 + SRV テーブル 1 = 63 DWORD（上限 64）。
        commands->SetPipelineState(resources_->GetPipelineState());
        commands->SetGraphicsRootSignature(resources_->GetRootSignature());
        commands->SetGraphicsRoot32BitConstants(0, 40, &constants, 0);
        commands->SetGraphicsRoot32BitConstants(2, 14, lightConstants.data(), 0);
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
