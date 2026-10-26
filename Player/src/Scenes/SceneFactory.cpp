#include "SceneFactory.h"
#include <SceneRuntime/Project/ProjectSettings.h>
#include "AuthoredScene.h"
#include <utility>

namespace Player
{
    SceneFactory::SceneFactory(std::filesystem::path root, bool simulateInactive,
                               SceneRuntime::PlaybackFeatureFactory feature)
        : root_(std::move(root)), simulateInactive_(simulateInactive), feature_(std::move(feature))
    {
    }
    std::unique_ptr<Engine::IScene> SceneFactory::Create(std::string_view name)
    {
        std::string scene(name);
        if (!SceneRuntime::ProjectSettings::ScenePath(scene))
            scene = SceneRuntime::ProjectSettings::Load(root_).ResolveScene(name);
        if (scene.starts_with("Assets/Scenes/") && scene.ends_with(".json") && scene.find("..") == std::string::npos &&
            scene.find('\\') == std::string::npos && scene.find(':') == std::string::npos)
        {
            const auto path = std::filesystem::path(std::u8string(scene.begin(), scene.end()));
            return std::make_unique<AuthoredScene>(root_, path, environment_, simulateInactive_, true,
                                                   std::filesystem::path{}, feature_);
        }
        return {};
    }
}
