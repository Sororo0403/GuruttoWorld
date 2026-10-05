#include <SceneRuntime/SceneWorld.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <Engine/Core/Log.h>
#include <format>
#include <stdexcept>
#include <utility>

namespace SceneRuntime
{
    bool SceneWorld::Initialize(Engine::DirectX12Renderer& renderer, const std::filesystem::path& assetsRoot,
        const std::filesystem::path& layoutPath, const std::filesystem::path& shaderPath)
    {
        if (!models_.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), shaderPath)) return false;
        try
        {
            auto layout = SceneLayout::Load(layoutPath);
            std::vector<Engine::Object3D> objects;
            objects.reserve(layout.objects.size());
            for (const auto& placement : layout.objects)
            {
                Engine::Object3D object;
                const auto model = models_.Load(assetsRoot / placement.model);
                if (!model) throw std::runtime_error("Model could not be loaded: " + placement.id);
                object.SetModel(model);
                if (!object.SetTransform(placement.position, placement.rotation, placement.scale))
                    throw std::runtime_error("Invalid transform: " + placement.id);
                objects.push_back(std::move(object));
            }
            layout_ = std::move(layout);
            objects_ = std::move(objects);
            return true;
        }
        catch (const std::exception& error)
        {
            Engine::Log::Error(std::format("Scene could not be loaded: {}", error.what()));
            return false;
        }
    }

    void SceneWorld::Draw(ID3D12GraphicsCommandList* commands, const Engine::Camera& camera,
        const Engine::DirectionalLight& light) const
    {
        for (const auto& object : objects_) object.Draw(commands, camera, light);
    }
}
