#pragma once
#include <Engine/Scenes/IScene.h>

namespace App
{
    class TitleScene final : public Engine::IScene
    {
    public:
        /// <summary>
        /// タイトルシーンを初期化します。
        /// </summary>
        bool Initialize(Engine::DirectX12Renderer& renderer) override;
        /// <summary>
        /// Enter キーでゲームへの遷移を要求します。
        /// </summary>
        std::string Update(double deltaSeconds, const Engine::Keyboard& keyboard) override;
        /// <summary>
        /// タイトルの背景と開発用案内を描画します。
        /// </summary>
        Engine::RenderResult Draw(Engine::DirectX12Renderer& renderer) override;
    };
}
