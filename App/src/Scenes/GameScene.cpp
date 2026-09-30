#include "GameScene.h"
#include "GameSettings.h"
#include <cmath>
#include <numbers>
#include <utility>
#if defined(_DEBUG) || defined(ENGINE_DEVELOPMENT)
#include "../DevTools/DebugPanel.h"
#include "../DevTools/AudioPanel.h"
#include "../DevTools/InputPanel.h"
#include "../DevTools/LightingPanel.h"
#include "../DevTools/UvTransformPanel.h"
#include <imgui.h>
#endif

namespace App
{
    GameScene::GameScene(std::filesystem::path root) : root_(std::move(root)) {}
    GameScene::~GameScene() { gamepad_.StopVibration(); }
    bool GameScene::Initialize(Engine::DirectX12Renderer& renderer)
    {
        const auto shaderPath = root_ / "Shaders" / "Mesh.hlsl";
        const auto modelPath = root_ / "Assets" / "Models" / "Cube.obj";
        const auto texturePath = root_ / "Assets" / "Textures" / "Checker.png";
        const auto spriteShaderPath = root_ / "Shaders" / "Sprite.hlsl";
        if (!modelReady_)
        {
            if (!modelManager_.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), shaderPath))
            {
                return false;
            }
            models_[0] = modelManager_.Load(modelPath);
            models_[1] = modelManager_.Load(modelPath.parent_path() / "Pyramid.obj");
            if (!models_[0] || !models_[1]) return false;
            // 同じパスを再要求しても、GPU リソースを含む既存モデルが返ります。
            secondModel_.SetModel(modelManager_.Load(modelPath));
            modelReady_ = true;
        }
        if (!spriteReady_)
        {
            if (!textureManager_.Initialize(renderer.GetDevice(), renderer.GetCommandQueue()) ||
                !sprite_.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), textureManager_.Load(texturePath), spriteShaderPath) ||
                !croppedSprite_.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), textureManager_.Load(texturePath), spriteShaderPath))
            {
                return false;
            }
            const auto particleShader = shaderPath.parent_path() / "Particle.hlsl";
            if (!particles_.CreateGroup("Checker", renderer.GetDevice(), renderer.GetCommandQueue(), textureManager_.Load(texturePath), particleShader) ||
                !particles_.CreateGroup("Glow", renderer.GetDevice(), renderer.GetCommandQueue(), textureManager_.Load({}), particleShader))
                return false;
            spriteReady_ = true;
        }
        return true;
    }
    std::string GameScene::Update(double deltaSeconds, const Engine::Keyboard& keyboard)
    {
        if (!audioAttempted_)
        {
            audioAttempted_ = true;
            if (audio_.Initialize())
            {
                sound_ = audio_.Load(root_ / "Assets" / "Audio" / "Sample.wav");
                audio_.SetVolume(sound_, GameSettings::Load(GameSettings::UserPath()).SampleVolume());
            }
        }
        gamepad_.Update(keyboard.IsActive());
#if defined(_DEBUG) || defined(ENGINE_DEVELOPMENT)
        frameKeyboard_ = &keyboard;
        cameraDeltaSeconds_ = deltaSeconds;
        if (!keyboard.IsActive()) cameraPanel_.CancelDrag();
#endif
        // スペースキーまたはコントローラーの A ボタンで再生します。
        bool captureKeyboard = false;
#if defined(_DEBUG) || defined(ENGINE_DEVELOPMENT)
        captureKeyboard = ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureKeyboard;
#endif
        bool editingUi = false;
#if defined(_DEBUG) || defined(ENGINE_DEVELOPMENT)
        editingUi = ImGui::GetCurrentContext() != nullptr && (ImGui::IsAnyItemActive() || ImGui::GetIO().WantTextInput);
#endif
        if (!editingUi && keyboard.IsPressed(DIK_ESCAPE)) return "Title";
        if ((keyboard.IsPressed(DIK_SPACE) || gamepad_.IsPressed(XINPUT_GAMEPAD_A)) && !captureKeyboard)
        {
            audio_.Play(sound_);
        }
        if (gamepad_.IsPressed(XINPUT_GAMEPAD_A) && !captureKeyboard)
        {
            gamepad_.Vibrate(0.3f, 0.3f, 0.25f);
        }
        if (gamepad_.IsPressed(XINPUT_GAMEPAD_B))
        {
            gamepad_.StopVibration();
        }
        if (!captureKeyboard)
        {
            if (keyboard.IsPressed(DIK_1)) selectedModel_ = 0;
            if (keyboard.IsPressed(DIK_2)) selectedModel_ = 1;
        }
        particles_.Update(deltaSeconds);
        if (spriteReady_)
        {
            emissionSeconds_ += deltaSeconds;
            while (emissionSeconds_ >= 0.08)
            {
                emissionSeconds_ -= 0.08;
                Engine::Particle particle;
                particle.position = { -1.1f, -0.6f, 0.0f };
                particle.velocity = { static_cast<float>(std::sin(rotationY_)) * 0.3f, 0.65f, 0.0f };
                particle.lifetime = 1.5f;
                particle.color = { 0.3f, 0.7f, 1.0f, 0.8f };
                particles_.Emit("Checker", particle);
                particle.position = { 0.7f, -0.5f, -0.8f };
                particle.color = { 1.0f, 0.4f, 0.1f, 0.65f };
                particles_.Emit("Glow", particle);
            }
        }
        if (rotating_)
        {
            const double angularSpeed = static_cast<double>(speedDegrees_) * std::numbers::pi / 180.0;
            rotationY_ = std::fmod(rotationY_ + angularSpeed * deltaSeconds, 2.0 * std::numbers::pi);
        }
        return {};
    }
    Engine::RenderResult GameScene::Draw(Engine::DirectX12Renderer& renderer)
    {
        return renderer.Render(backgroundColor_, [&](ID3D12GraphicsCommandList* commands, float aspectRatio)
        {
            camera_.SetAspectRatio(aspectRatio);
            model_.SetModel(models_[selectedModel_]);
            model_.SetTransform({ 0.0f, 0.0f, 0.0f }, { -0.3f, static_cast<float>(rotationY_), 0.0f }, { 0.75f, 0.75f, 0.75f });
            secondModel_.SetTransform({ 1.25f, -0.65f, 0.4f }, { 0.0f, -static_cast<float>(rotationY_), 0.0f }, { 0.35f, 0.35f, 0.35f });
            model_.Draw(commands, camera_, light_, modelUv_);
            secondModel_.Draw(commands, camera_, light_, modelUv_);
            particles_.Draw(commands, camera_);
            Engine::SpriteDrawParameters spriteParameters;
            spriteParameters.position = { static_cast<float>(renderer.GetWidth()) - 192.0f, 32.0f };
            spriteParameters.size = { 160.0f, 160.0f };
            spriteParameters.color = { 1.0f, 1.0f, 1.0f, 0.8f };
            spriteParameters.uvTransform = spriteUv_;
            sprite_.Draw(commands, renderer.GetWidth(), renderer.GetHeight(), spriteParameters);
            // 同じ画像を共有し、別の位置・UV 範囲で描画します。
            spriteParameters.position[0] -= 144.0f;
            spriteParameters.size = { 128.0f, 128.0f };
            spriteParameters.uvRect = { 0.0f, 0.0f, 0.5f, 0.5f };
            croppedSprite_.Draw(commands, renderer.GetWidth(), renderer.GetHeight(), spriteParameters);
        }, [this]() { DrawDebugUi(); });
    }
    void GameScene::DrawDebugUi()
    {
#if defined(_DEBUG) || defined(ENGINE_DEVELOPMENT)
        if (frameKeyboard_ != nullptr) cameraPanel_.Draw(debugCamera_, *frameKeyboard_, cameraDeltaSeconds_);
        App::AudioPanel::Draw(audio_, sound_);
        App::InputPanel::Draw(gamepad_);
        App::DebugPanel::Draw(rotationY_, speedDegrees_, rotating_, backgroundColor_);
        App::LightingPanel::Draw(light_);
        App::UvTransformPanel::Draw(modelUv_, spriteUv_);
#endif
    }
}
