#pragma once
#include <Engine/Scenes/IScene.h>
#include <Engine/Graphics/Renderers/SpriteRenderer.h>
#include <Engine/Graphics/Models/ModelManager.h>
#include <Engine/Graphics/Models/Object3D.h>
#include <Engine/Graphics/Camera.h>

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
        /// タイトル画像と CC0 建物モデルを初期化します。
        /// </summary>
        bool Initialize(Engine::DirectX12Renderer& renderer) override;
        /// <summary>
        /// Enter キーでゲームへの遷移を要求します。
        /// </summary>
        std::string Update(double deltaSeconds, const Engine::Keyboard& keyboard) override;
        /// <summary>
        /// 全構成で建物モデル、タイトル、開始案内を画面サイズに合わせて描画します。
        /// </summary>
        Engine::RenderResult Draw(Engine::DirectX12Renderer& renderer) override;
    private:
        std::filesystem::path root_;
        Engine::SpriteRenderer title_;
        Engine::ModelManager modelManager_;
        Engine::Object3D building_;
        Engine::Camera camera_;
        Engine::DirectionalLight light_;
    };
}
