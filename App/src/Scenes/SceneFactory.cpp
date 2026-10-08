#include "SceneFactory.h"
#include "TitleBindings.h"
#include <SceneRuntime/ProjectSettings.h>
#include "AuthoredScene.h"
#include "TitleScene.h"
#include <utility>

namespace App
{
    SceneFactory::SceneFactory(std::filesystem::path root) : root_(std::move(root)) {}
    std::unique_ptr<Engine::IScene> SceneFactory::Create(std::string_view name)
    {
        std::string scene(name);
        if(name=="Title") scene=SceneRuntime::ProjectSettings::Load(root_).startupScene;
        if(name=="Game" || name=="EngineDemo") scene="Assets/Scenes/Game.json";
        if(scene.starts_with("Assets/Scenes/") && scene.ends_with(".json") && scene.find("..") == std::string::npos && scene.find('\\')==std::string::npos && scene.find(':')==std::string::npos) {
            const auto path=std::filesystem::path(std::u8string(scene.begin(),scene.end()));
            const auto layout=SceneRuntime::SceneLayout::Load(root_/path);
            if(TitleBindings::Configuration(layout)) {
                auto title=std::make_unique<TitleScene>(root_,!visited_.contains(scene),path);
                visited_.insert(scene); return title;
            }
            return std::make_unique<AuthoredScene>(root_,path);
        }
        return {};
    }
}
