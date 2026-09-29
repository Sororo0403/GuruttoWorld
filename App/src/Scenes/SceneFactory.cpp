#include "SceneFactory.h"
#include "GameScene.h"
#include "TitleScene.h"
#include <utility>

namespace App
{
    SceneFactory::SceneFactory(std::filesystem::path root) : root_(std::move(root)) {}
    std::unique_ptr<Engine::IScene> SceneFactory::Create(std::string_view name)
    {
        if (name == "Title") return std::make_unique<TitleScene>(root_);
        if (name == "Game") return std::make_unique<GameScene>(root_);
        return {};
    }
}
