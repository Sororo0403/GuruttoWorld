#pragma once
#include <Engine/Scenes/IScene.h>
#include "TitleUi.h"
#include "TitleMenu.h"
#include <Engine/Input/Gamepad.h>
#include <SceneRuntime/TitleEnvironment.h>
#include "TitleAudio.h"

namespace App
{
    class TitleScene final : public Engine::IScene
    {
    public:
        /// <summary>
        /// タイトル画像とシェーダーの基準フォルダーを受け取ります。
        /// </summary>
        explicit TitleScene(std::filesystem::path root, bool playIntro = true);
        /// <summary>
        /// タイトル画像と CC0 モデルの街並みを初期化します。
        /// </summary>
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
        TitleMenuInput ReadMenuInput(const Engine::Keyboard& keyboard) const;
        std::filesystem::path root_;
        TitleUi ui_;
        TitleMenu menu_;
        Engine::Gamepad gamepad_;
        SceneRuntime::TitleEnvironment environment_;
        TitleAudio audio_;
    };
}
