#include <Engine/Graphics/Renderers/ModelRenderer.h>
#include <Engine/Graphics/Models/ModelLoader.h>
#include <limits>

namespace Engine
{
    void ModelRenderer::DrawShadow(ID3D12GraphicsCommandList* commands,const DirectX::XMFLOAT4X4& world,const ShadowMap& shadow) const
    {
        for (const auto& mesh:meshes_) mesh->DrawShadow(commands,world,shadow);
    }
    bool ModelRenderer::Initialize(ID3D12Device* device, ID3D12CommandQueue* queue,
        const std::filesystem::path& modelPath, const std::filesystem::path& shaderPath)
    {
        if (!meshes_.empty() || device == nullptr || queue == nullptr)
        {
            return false;
        }
        std::vector<MeshData> data;
        if (!ModelLoader::Load(modelPath, data))
        {
            return false;
        }
        auto resources = std::make_shared<MeshResources>();
        if (!resources->Initialize(device, shaderPath))
        {
            return false;
        }
        std::vector<std::unique_ptr<MeshRenderer>> loaded;
        for (const auto& mesh : data)
        {
            auto renderer = std::make_unique<MeshRenderer>();
            if (!renderer->Initialize(device, queue, mesh, resources))
            {
                return false;
            }
            loaded.push_back(std::move(renderer));
        }
        std::vector<DirectX::XMFLOAT3> positions;
        for (const auto& mesh : data)
            for (auto index : mesh.indices)
            {
                const auto& p = mesh.vertices[index].position;
                positions.push_back({ p[0], p[1], p[2] });
            }
        if (positions.empty()) return false;
        DirectX::BoundingBox::CreateFromPoints(bounds_, positions.size(), positions.data(), sizeof(DirectX::XMFLOAT3));
        trianglePositions_ = std::move(positions);
        meshes_ = std::move(loaded);
        return true;
    }

    void ModelRenderer::Draw(ID3D12GraphicsCommandList* commands, const DirectX::XMFLOAT4X4& world,
        const DirectX::XMFLOAT4X4& viewProjection, const DirectionalLight& light,
        const std::array<float, 3>& cameraPosition, const UvTransform& uvTransform) const
    {
        for (const auto& mesh : meshes_)
        {
            mesh->Draw(commands, world, viewProjection, light, cameraPosition, uvTransform);
        }
    }
    bool ModelRenderer::IntersectRay(DirectX::FXMVECTOR origin, DirectX::FXMVECTOR direction,
        float& distance) const
    {
        float broadDistance;
        if (!bounds_.Intersects(origin, direction, broadDistance)) return false;
        float closest = (std::numeric_limits<float>::max)();
        bool found = false;
        for (size_t index = 0; index + 2 < trianglePositions_.size(); index += 3)
        {
            float hit;
            if (DirectX::TriangleTests::Intersects(origin, direction,
                DirectX::XMLoadFloat3(&trianglePositions_[index]),
                DirectX::XMLoadFloat3(&trianglePositions_[index + 1]),
                DirectX::XMLoadFloat3(&trianglePositions_[index + 2]), hit) && hit < closest)
            {
                closest = hit;
                found = true;
            }
        }
        if (found) distance = closest;
        return found;
    }
}
