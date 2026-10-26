#pragma once
#include <Engine/Scenes/IScene.h>
#include <SceneRuntime/Scene/SceneEnvironment.h>
#include <SceneRuntime/Gameplay/GamePlayback.h>
#include <Engine/Input/Gamepad.h>
#include <Engine/Input/InputActions.h>
#include <memory>
namespace Player
{
    class AuthoredScene final : public Engine::IScene
    {
    public:
        /// <summary>共有環境と、診断時だけ非アクティブでもシミュレーションする設定を保持します。</summary>
        AuthoredScene(std::filesystem::path root, std::filesystem::path scene,
                      std::shared_ptr<SceneRuntime::SceneEnvironment> environment =
                          std::make_shared<SceneRuntime::SceneEnvironment>(),
                      bool simulateInactive = false, bool playIntro = true, std::filesystem::path settingsPath = {})
            : root_(std::move(root)), scene_(std::move(scene)), environmentOwner_(std::move(environment)),
              environment_(*environmentOwner_), simulateInactive_(simulateInactive), playIntro_(playIntro),
              settingsPath_(std::move(settingsPath))
        {
        }
        /// <summary>現在実行しているシーンのパスを返します。</summary>
        std::string ActiveSceneName() const override
        {
            return environment_.ActiveSceneName();
        }
        /// <summary>プロジェクトの入力設定とシーン、音声、スクリプトを準備します。</summary>
        bool Initialize(Engine::DirectX12Renderer&) override;
        /// <summary>入力、UI、シミュレーションを更新し、終了要求をウィンドウへ通知します。</summary>
        std::string Update(double, const Engine::Keyboard&) override;
        /// <summary>確定したシーンの描画資源を準備して表示します。</summary>
        Engine::RenderResult Draw(Engine::DirectX12Renderer&) override;

    private:
        /// <summary>ウィンドウ入力またはゲーム側から注入された入力をシーンのUIへ渡します。</summary>
        void UpdateUi(const Engine::Keyboard& keyboard, bool active);
        std::filesystem::path root_, scene_, settingsPath_;
        std::shared_ptr<SceneRuntime::SceneEnvironment> environmentOwner_;
        SceneRuntime::SceneEnvironment& environment_;
        Engine::Gamepad gamepad_;
        Engine::InputActions input_;
        unsigned int width_ = 0, height_ = 0;
        SceneRuntime::GamePlayback playback_;
        bool simulateInactive_ = false, playIntro_ = true;
    };
}
