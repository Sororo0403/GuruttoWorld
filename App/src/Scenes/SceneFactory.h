#pragma once
#include <Engine/Scenes/ISceneFactory.h>
#include <filesystem>

namespace App
{
    class SceneFactory final : public Engine::ISceneFactory
    {
    public:
        /// <summary>
        /// アセットの基準フォルダを保持します。
        /// </summary>
        explicit SceneFactory(std::filesystem::path root);
        /// <summary>
        /// アプリ固有の識別子から具体的なシーンを生成します。
        /// </summary>
        std::unique_ptr<Engine::IScene> Create(std::string_view name) override;
    private:
        std::filesystem::path root_;
        bool titleVisited_ = false;
    };
}
