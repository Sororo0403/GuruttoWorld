#pragma once
#include <Engine/Scenes/IScene.h>
#include "AuthoredScene.h"
#include <memory>

namespace App
{
    class TitleScene final : public Engine::IScene
    {
    public:
        /// <summary>
        /// タイトルの資源と共有環境、診断時だけ背景でもシミュレーションする設定を受け取ります。
        /// </summary>
        explicit TitleScene(std::filesystem::path root, bool playIntro = true, std::filesystem::path scene="Assets/Scenes/TitleStreet.json",std::shared_ptr<SceneRuntime::SceneEnvironment> environment=std::make_shared<SceneRuntime::SceneEnvironment>(),bool simulateInactive=false);
        /// <summary>
        /// タイトル画像と CC0 モデルの街並みを初期化します。
        /// </summary>
        std::string ActiveSceneName() const override {return implementation_.ActiveSceneName();}
        bool Initialize(Engine::DirectX12Renderer& renderer) override;
        /// <summary>
        /// キーボード・ゲームパッドでメニューを操作し、開始または通常終了を要求します。
        /// </summary>
        std::string Update(double deltaSeconds, const Engine::Keyboard& keyboard) override;
        /// <summary>
        /// 全構成で建物モデル、タイトル、開始案内を画面サイズに合わせて描画します。
        /// </summary>
        Engine::RenderResult Draw(Engine::DirectX12Renderer& renderer) override;
    private:
        AuthoredScene implementation_;
    };
}
