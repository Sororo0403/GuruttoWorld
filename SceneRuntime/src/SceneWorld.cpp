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
    std::string SceneWorld::NewId()
    {
        for (;;)
        {
            const auto id = "object-" + std::to_string(nextObjectId_++);
            if (std::none_of(layout_.objects.begin(), layout_.objects.end(),
                [&](const auto& placement) { return placement.id == id; })) return id;
        }
    }

    void SceneWorld::Append(ScenePlacement placement, Engine::Object3D object)
    {
        layout_.objects.reserve(layout_.objects.size() + 1);
        objects_.reserve(objects_.size() + 1);
        layout_.objects.push_back(std::move(placement));
        objects_.push_back(std::move(object));
    }

    bool SceneWorld::AddObject(ScenePlacement placement, const std::filesystem::path& assetsRoot,
        std::string& createdId, std::string& error)
    {
        createdId.clear();
        try
        {
            if (!modelsReady_) throw std::runtime_error("Scene renderer is unavailable");
            if (placement.id.empty()) placement.id = NewId();
            if (std::any_of(layout_.objects.begin(), layout_.objects.end(),
                [&](const auto& existing) { return existing.id == placement.id; }))
                throw std::runtime_error("Object ID already exists");
            if (placement.name.empty()) placement.name = placement.model.stem().string();
            SceneLayout validation;
            validation.objects.push_back(placement);
            static_cast<void>(validation.Serialize());
            Engine::Object3D object;
            if (!object.SetTransform(placement.position, placement.rotation, placement.scale))
                throw std::runtime_error("Invalid transform");
            const auto model = models_.Load(assetsRoot / placement.model);
            if (!model) throw std::runtime_error("Model could not be loaded");
            object.SetModel(model);
            const auto id = placement.id;
            Append(std::move(placement), std::move(object));
            createdId = id;
            error.clear();
            return true;
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return false;
        }
    }

    bool SceneWorld::DuplicateObject(std::string_view id, const std::array<float, 3>& offset,
        std::string& createdId, std::string& error)
    {
        createdId.clear();
        const auto found = std::find_if(layout_.objects.begin(), layout_.objects.end(),
            [id](const auto& placement) { return placement.id == id; });
        if (found == layout_.objects.end()) { error = "Object no longer exists"; return false; }
        auto placement = *found;
        auto object = objects_[static_cast<size_t>(found - layout_.objects.begin())];
        placement.id = NewId();
        placement.name += " copy";
        for (size_t i = 0; i < 3; ++i) placement.position[i] += offset[i];
        if (!object.SetTransform(placement.position, placement.rotation, placement.scale))
        {
            error = "Invalid duplicate offset";
            return false;
        }
        const auto newId = placement.id;
        Append(std::move(placement), std::move(object));
        createdId = newId;
        error.clear();
        return true;
    }

    bool SceneWorld::RemoveObject(std::string_view id)
    {
        const auto found = std::find_if(layout_.objects.begin(), layout_.objects.end(),
            [id](const auto& placement) { return placement.id == id; });
        if (found == layout_.objects.end()) return false;
        const auto index = static_cast<size_t>(found - layout_.objects.begin());
        layout_.objects.erase(found);
        objects_.erase(objects_.begin() + static_cast<std::ptrdiff_t>(index));
        return true;
    }
}
