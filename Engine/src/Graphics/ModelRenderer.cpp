#include <Engine/Graphics/ModelRenderer.h>
#include <Engine/Graphics/ModelLoader.h>

namespace Engine
{
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
        std::vector<std::unique_ptr<MeshRenderer>> loaded;
        for (const auto& mesh : data)
        {
            auto renderer = std::make_unique<MeshRenderer>();
            if (!renderer->Initialize(device, queue, mesh, shaderPath))
            {
                return false;
            }
            loaded.push_back(std::move(renderer));
        }
        meshes_ = std::move(loaded);
        return true;
    }

    void ModelRenderer::Draw(ID3D12GraphicsCommandList* commands, const DirectX::XMFLOAT4X4& world,
        const DirectX::XMFLOAT4X4& viewProjection, const DirectionalLight& light,
        const std::array<float, 3>& cameraPosition, const UVTransform& uvTransform) const
    {
        for (const auto& mesh : meshes_)
        {
            mesh->Draw(commands, world, viewProjection, light, cameraPosition, uvTransform);
        }
    }
}
