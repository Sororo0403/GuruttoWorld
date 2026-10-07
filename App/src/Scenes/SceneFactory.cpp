#include "SceneFactory.h"
#include "GameScene.h"
#include "AuthoredScene.h"
#include "TitleScene.h"
#include <utility>

namespace App
{
    SceneFactory::SceneFactory(std::filesystem::path root) : root_(std::move(root)) {}
    std::unique_ptr<Engine::IScene> SceneFactory::Create(std::string_view name)
    {
        if (name == "Title")
        {
            auto title = std::make_unique<TitleScene>(root_, !titleVisited_);
            titleVisited_ = true;
            return title;
        }
        if (name == "Game") return std::make_unique<AuthoredScene>(root_,"Assets/Scenes/Game.json");
        if (name == "EngineDemo") return std::make_unique<GameScene>(root_);
        if (name == "Assets/Scenes/TitleStreet.json") return Create("Title");
        if(name.starts_with("Assets/Scenes/") && name.ends_with(".json") && name.find("..") == std::string_view::npos)
            return std::make_unique<AuthoredScene>(root_,std::filesystem::path(std::u8string(name.begin(),name.end())));
        return {};
    }
}
