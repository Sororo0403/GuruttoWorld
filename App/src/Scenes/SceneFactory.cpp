#include "SceneFactory.h"
#include "GameScene.h"
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
        if (name == "Game") return std::make_unique<GameScene>(root_);
        return {};
    }
}
