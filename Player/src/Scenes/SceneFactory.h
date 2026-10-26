#pragma once
#include <Engine/Scenes/ISceneFactory.h>
#include <filesystem>
#include <string>
#include <SceneRuntime/Scene/SceneEnvironment.h>

namespace Player
{
    class SceneFactory final : public Engine::ISceneFactory
    {
    public:
        /// <summary>
        /// アセットの基準フォルダと、診断時だけ背景でもシミュレーションする設定を保持します。
        /// </summary>
        explicit SceneFactory(std::filesystem::path root, bool simulateInactive = false);
        /// <summary>
        /// アプリ固有の識別子から具体的なシーンを生成します。
        /// </summary>
        std::unique_ptr<Engine::IScene> Create(std::string_view name) override;
        /// <summary>
        /// 診断計測用に、シーンで共有する環境の参照を返します。
        /// </summary>
        SceneRuntime::SceneEnvironment& Environment() const noexcept
        {
            return *environment_;
        }

    private:
        std::filesystem::path root_;
        bool simulateInactive_ = false;
        std::shared_ptr<SceneRuntime::SceneEnvironment> environment_ =
            std::make_shared<SceneRuntime::SceneEnvironment>();
    };
}
