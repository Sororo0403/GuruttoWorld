#pragma once
#include <Engine/Scenes/IScene.h>
#include <Engine/Graphics/Renderers/SpriteRenderer.h>

namespace App
{
    class TitleScene final : public Engine::IScene
    {
    public:
        /// <summary>
        /// タイトル画像とシェーダーの基準フォルダーを受け取ります。
        /// </summary>
        explicit TitleScene(std::filesystem::path root);
        /// <summary>
        /// タイトルシーンを初期化します。
        /// </summary>
        bool Initialize(Engine::DirectX12Renderer& renderer) override;
        /// <summary>
        /// Enter キーでゲームへの遷移を要求します。
        /// </summary>
        std::string Update(double deltaSeconds, const Engine::Keyboard& keyboard) override;
        /// <summary>
        /// 全構成でタイトルと開始案内を画面サイズに合わせて描画します。
        /// </summary>
        Engine::RenderResult Draw(Engine::DirectX12Renderer& renderer) override;
    private:
        std::filesystem::path root_;
        Engine::SpriteRenderer title_;
    };
}
