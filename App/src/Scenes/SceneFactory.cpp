#include "SceneFactory.h"
#include "TitleBindings.h"
#include <SceneRuntime/ProjectSettings.h>
#include "AuthoredScene.h"
#include "TitleScene.h"
#include <utility>

namespace App
{
    SceneFactory::SceneFactory(std::filesystem::path root, bool simulateInactive)
        : root_(std::move(root)), simulateInactive_(simulateInactive)
    {
    }
    std::unique_ptr<Engine::IScene> SceneFactory::Create(std::string_view name)
    {
        std::string scene(name);
        if (name == "Title")
            scene = SceneRuntime::ProjectSettings::Load(root_).startupScene;
        else if (!SceneRuntime::ProjectSettings::ScenePath(scene))
            scene = SceneRuntime::ProjectSettings::Load(root_).ResolveScene(name);
        if (scene.starts_with("Assets/Scenes/") && scene.ends_with(".json") && scene.find("..") == std::string::npos &&
            scene.find('\\') == std::string::npos && scene.find(':') == std::string::npos)
        {
            const auto path = std::filesystem::path(std::u8string(scene.begin(), scene.end()));
            const auto layout = SceneRuntime::SceneLayout::Load(root_ / path);
            if (TitleBindings::Configuration(layout))
            {
                return std::make_unique<TitleScene>(root_, true, path, environment_, simulateInactive_);
            }
            return std::make_unique<AuthoredScene>(root_, path, environment_, simulateInactive_);
        }
        return {};
    }
}
