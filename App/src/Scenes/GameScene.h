#pragma once
#include <Engine/Scenes/IScene.h>
#include <Engine/Audio/AudioSystem.h>
#include <Engine/Input/Keyboard.h>
#include <Engine/Input/Gamepad.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <Engine/Graphics/Models/ModelManager.h>
#include <Engine/Graphics/Models/Object3D.h>
#include <Engine/Graphics/Models/ParticleSystem.h>
#include <Engine/Graphics/Renderers/SpriteRenderer.h>
#include <Engine/Graphics/Resources/TextureManager.h>
#include <Windows.h>
#if defined(_DEBUG) || defined(ENGINE_DEVELOPMENT)
#include "../DevTools/CameraPanel.h"
#endif

namespace App
{
    class GameScene final : public Engine::IScene
    {
    public:
        /// <summary>
        /// アセットの基準フォルダを受け取ります。
        /// </summary>
        explicit GameScene(std::filesystem::path root);
        /// <summary>
        /// コントローラーの振動を停止し、シーンのリソースを解放します。
        /// </summary>
        ~GameScene() override;
        /// <summary>
        /// モデル・テクスチャ・パーティクルを初期化します。
        /// </summary>
        bool Initialize(Engine::DirectX12Renderer& renderer) override;
        /// <summary>
        /// ゲームを更新します。Escape キーでタイトルへの遷移を要求します。
        /// </summary>
        std::string Update(double deltaSeconds, const Engine::Keyboard& keyboard) override;
        /// <summary>
        /// ゲームシーンを描画します。
        /// </summary>
        Engine::RenderResult Draw(Engine::DirectX12Renderer& renderer) override;
    private:
        /// <summary>
        /// Debug / Development の操作パネルを構築します。
        /// </summary>
        void DrawDebugUi();
        std::filesystem::path root_;
        Engine::AudioSystem audio_;
        Engine::SoundHandle sound_ = 0;
        bool audioAttempted_ = false;
        Engine::Gamepad gamepad_;
        Engine::ModelManager modelManager_;
        Engine::Object3D model_;
        Engine::Object3D secondModel_;
        std::array<std::shared_ptr<const Engine::ModelRenderer>, 2> models_;
        size_t selectedModel_ = 0;
        bool modelReady_ = false;
        Engine::TextureManager textureManager_;
        Engine::SpriteRenderer sprite_;
        Engine::SpriteRenderer croppedSprite_;
        bool spriteReady_ = false;
        Engine::ParticleSystem particles_;
        double emissionSeconds_ = 0.0;
        double rotationY_ = 0.0;
        float speedDegrees_ = 90.0f;
        bool rotating_ = true;
        std::array<float, 4> backgroundColor_{ 0.08f, 0.20f, 0.40f, 1.0f };
        Engine::DirectionalLight light_;
        Engine::UvTransform modelUv_;
        Engine::UvTransform spriteUv_;
#if !defined(_DEBUG) && !defined(ENGINE_DEVELOPMENT)
        Engine::Camera camera_;
#endif
#if defined(_DEBUG) || defined(ENGINE_DEVELOPMENT)
        Engine::DebugCamera debugCamera_;
        Engine::Camera& camera_ = debugCamera_.GetCamera();
        App::CameraPanel cameraPanel_;
        const Engine::Keyboard* frameKeyboard_ = nullptr;
        double cameraDeltaSeconds_ = 0.0;
#endif
    };
}
