#include <SceneRuntime/SceneWorld.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <Engine/Core/Log.h>
#include <format>
#include <stdexcept>
#include <utility>
#include <algorithm>
#include <cmath>

namespace SceneRuntime
{
    bool SceneWorld::Initialize(Engine::DirectX12Renderer& renderer, const std::filesystem::path& assetsRoot,
        const std::filesystem::path& layoutPath, const std::filesystem::path& shaderPath, std::string* diagnostic)
    {
        modelsReady_ = models_.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), shaderPath);
        if (!modelsReady_)
        {
            if (diagnostic) *diagnostic = "Scene renderer could not be initialized. Check shaders and restart.";
            return false;
        }
        std::string error;
        const bool loaded = Reload(assetsRoot, layoutPath, error);
        if (diagnostic) *diagnostic = error;
        return loaded;
    }

    bool SceneWorld::Reload(const std::filesystem::path& assetsRoot, const std::filesystem::path& layoutPath,
        std::string& error)
    {
        if (!modelsReady_)
        {
            error = "Scene renderer is unavailable. Check shaders and restart.";
            return false;
        }
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
            error.clear();
            return true;
        }
        catch (const std::exception& errorException)
        {
            error = std::string("Scene could not be loaded: ") + errorException.what();
            Engine::Log::Error(error);
            return false;
        }
    }

    void SceneWorld::Draw(ID3D12GraphicsCommandList* commands, const Engine::Camera& camera,
        const Engine::DirectionalLight& light) const
    {
        for (const auto& object : objects_) object.Draw(commands, camera, light);
    }

    bool SceneWorld::SetTransform(std::string_view id, const std::array<float, 3>& position,
        const std::array<float, 3>& rotation, const std::array<float, 3>& scale)
    {
        const auto found = std::find_if(layout_.objects.begin(), layout_.objects.end(),
            [id](const ScenePlacement& placement) { return placement.id == id; });
        if (found == layout_.objects.end() ||
            !std::all_of(scale.begin(), scale.end(), [](float value)
                { return std::isfinite(value) && std::abs(value) >= 0.000001f; })) return false;
        const auto index = static_cast<size_t>(found - layout_.objects.begin());
        if (!objects_[index].SetTransform(position, rotation, scale)) return false;
        found->position = position;
        found->rotation = rotation;
        found->scale = scale;
        return true;
    }
}
