#pragma once
#include <Engine/Scenes/IScene.h>
#include <SceneRuntime/SceneEnvironment.h>
#include <SceneRuntime/GamePlayback.h>
#include <Engine/Input/Gamepad.h>
#include <Engine/Input/InputActions.h>
#include <memory>
namespace App
{
    class AuthoredScene final : public Engine::IScene
    {
    public:
        /// <summary>共有環境と、診断時だけ非アクティブでもシミュレーションする設定を保持します。</summary>
        AuthoredScene(std::filesystem::path root, std::filesystem::path scene,
                      std::shared_ptr<SceneRuntime::SceneEnvironment> environment =
                          std::make_shared<SceneRuntime::SceneEnvironment>(),
                      bool simulateInactive = false, bool playIntro = true,
                      std::filesystem::path settingsPath = SceneRuntime::GameSettings::UserPath())
            : root_(std::move(root)), scene_(std::move(scene)), environmentOwner_(std::move(environment)),
              environment_(*environmentOwner_), simulateInactive_(simulateInactive), playIntro_(playIntro),
              settingsPath_(std::move(settingsPath))
        {
        }
        std::string ActiveSceneName() const override
        {
            return environment_.ActiveSceneName();
        }
        bool Initialize(Engine::DirectX12Renderer&) override;
        std::string Update(double, const Engine::Keyboard&) override;
        Engine::RenderResult Draw(Engine::DirectX12Renderer&) override;

    private:
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
