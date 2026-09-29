#include <Engine/Core/Application.h>
#include <Engine/Audio/AudioSystem.h>
#include <Engine/Input/Keyboard.h>
#include <Engine/Input/Gamepad.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <Engine/Graphics/Models/ModelManager.h>
#include <Engine/Graphics/Models/Object3D.h>
#include <Engine/Graphics/Renderers/SpriteRenderer.h>
#include <Engine/Graphics/Resources/TextureManager.h>
#include <Windows.h>
#if defined(_DEBUG) || defined(ENGINE_DEVELOPMENT)
#include "DevTools/DebugPanel.h"
#include "DevTools/AudioPanel.h"
#include "DevTools/InputPanel.h"
#include "DevTools/CameraPanel.h"
#include <imgui.h>
#include "DevTools/LightingPanel.h"
#include "DevTools/UvTransformPanel.h"
#endif

#include <cmath>
#include <filesystem>
#include <numbers>
#include <string>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    std::wstring executable(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (length == 0 || length >= executable.size())
    {
        return 1;
    }
    executable.resize(length);
    const auto shaderPath = std::filesystem::path(executable).parent_path() / "Shaders" / "Mesh.hlsl";

    const auto modelPath = std::filesystem::path(executable).parent_path() / "Assets" / "Models" / "Cube.obj";
    const auto texturePath = std::filesystem::path(executable).parent_path() / "Assets" / "Textures" / "Checker.png";

    const auto spriteShaderPath = std::filesystem::path(executable).parent_path() / "Shaders" / "Sprite.hlsl";

    Engine::ApplicationSettings settings;
    settings.title = L"WP1";

    Engine::AudioSystem audio;
    Engine::SoundHandle sound = 0;
    bool audioAttempted = false;
    Engine::Gamepad gamepad;
    // Run が GPU の完了を待ってから戻るため、描画リソースはその後で安全に破棄できます。
    Engine::ModelManager modelManager;
    Engine::Object3D model;
    Engine::Object3D secondModel;
    std::array<std::shared_ptr<const Engine::ModelRenderer>, 2> models;
    size_t selectedModel = 0;
    bool modelReady = false;
    Engine::TextureManager textureManager;
    Engine::SpriteRenderer sprite;
    Engine::SpriteRenderer croppedSprite;
    bool spriteReady = false;
    double rotationY = 0.0;
    float speedDegrees = 90.0f;
    bool rotating = true;
    std::array<float, 4> backgroundColor{ 0.08f, 0.20f, 0.40f, 1.0f };
    Engine::DirectionalLight light;
    Engine::UvTransform modelUv;
    Engine::UvTransform spriteUv;
#if !defined(_DEBUG) && !defined(ENGINE_DEVELOPMENT)
    const std::array<float, 3> cameraPosition{ 0.0f, 0.0f, -3.5f };
#endif
    std::function<void()> debugUi;
#if defined(_DEBUG) || defined(ENGINE_DEVELOPMENT)
    Engine::DebugCamera debugCamera;
    App::CameraPanel cameraPanel;
    const Engine::Keyboard* frameKeyboard = nullptr;
    double cameraDeltaSeconds = 0.0;
    debugUi = [&]()
    {
        if (frameKeyboard != nullptr) cameraPanel.Draw(debugCamera, *frameKeyboard, cameraDeltaSeconds);
        App::AudioPanel::Draw(audio, sound);
        App::InputPanel::Draw(gamepad);
        App::DebugPanel::Draw(rotationY, speedDegrees, rotating, backgroundColor);
        App::LightingPanel::Draw(light);
        App::UvTransformPanel::Draw(modelUv, spriteUv);
    };
#endif
    Engine::ApplicationCallbacks callbacks;
    callbacks.update = [&](double deltaSeconds, const Engine::Keyboard& keyboard)
    {
        if (!audioAttempted)
        {
            audioAttempted = true;
            if (audio.Initialize())
            {
                sound = audio.Load(std::filesystem::path(executable).parent_path() / "Assets" / "Audio" / "Sample.wav");
                audio.SetVolume(sound, 0.25f);
            }
        }
        gamepad.Update(keyboard.IsActive());
#if defined(_DEBUG) || defined(ENGINE_DEVELOPMENT)
        frameKeyboard = &keyboard;
        cameraDeltaSeconds = deltaSeconds;
        if (!keyboard.IsActive()) cameraPanel.CancelDrag();
#endif
        // スペースキーまたはコントローラーの A ボタンで再生します。
        bool captureKeyboard = false;
#if defined(_DEBUG) || defined(ENGINE_DEVELOPMENT)
        captureKeyboard = ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureKeyboard;
#endif
        if ((keyboard.IsPressed(DIK_SPACE) || gamepad.IsPressed(XINPUT_GAMEPAD_A)) && !captureKeyboard)
        {
            audio.Play(sound);
        }
        if (gamepad.IsPressed(XINPUT_GAMEPAD_A) && !captureKeyboard)
        {
            gamepad.Vibrate(0.3f, 0.3f, 0.25f);
        }
        if (gamepad.IsPressed(XINPUT_GAMEPAD_B))
        {
            gamepad.StopVibration();
        }
        if (!captureKeyboard)
        {
            if (keyboard.IsPressed(DIK_1)) selectedModel = 0;
            if (keyboard.IsPressed(DIK_2)) selectedModel = 1;
        }
        if (rotating)
        {
            const double angularSpeed = static_cast<double>(speedDegrees) * std::numbers::pi / 180.0;
            rotationY = std::fmod(rotationY + angularSpeed * deltaSeconds, 2.0 * std::numbers::pi);
        }
    };
    callbacks.draw = [&](Engine::DirectX12Renderer& renderer)
    {
        if (!modelReady)
        {
            if (!modelManager.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), shaderPath))
            {
                return Engine::RenderResult::Failed;
            }
            models[0] = modelManager.Load(modelPath);
            models[1] = modelManager.Load(modelPath.parent_path() / "Pyramid.obj");
            if (!models[0] || !models[1]) return Engine::RenderResult::Failed;
            // 同じパスを再要求しても、GPU リソースを含む既存モデルが返ります。
            secondModel.SetModel(modelManager.Load(modelPath));
            modelReady = true;
        }
        if (!spriteReady)
        {
            if (!textureManager.Initialize(renderer.GetDevice(), renderer.GetCommandQueue()) ||
                !sprite.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), textureManager.Load(texturePath), spriteShaderPath) ||
                !croppedSprite.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), textureManager.Load(texturePath), spriteShaderPath))
            {
                return Engine::RenderResult::Failed;
            }
            spriteReady = true;
        }
        return renderer.Render(backgroundColor, [&](ID3D12GraphicsCommandList* commands, float aspectRatio)
        {
            using namespace DirectX;
#if defined(_DEBUG) || defined(ENGINE_DEVELOPMENT)
            const auto& cameraPosition = debugCamera.GetPosition();
            const XMMATRIX view = debugCamera.GetViewMatrix();
#else
            const XMMATRIX view = XMMatrixLookAtLH(XMVectorSet(cameraPosition[0], cameraPosition[1], cameraPosition[2], 1.0f),
                XMVectorZero(), XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
#endif
            const XMMATRIX projection = XMMatrixPerspectiveFovLH(XM_PIDIV4, aspectRatio, 0.1f, 100.0f);
            XMFLOAT4X4 viewProjection;
            XMStoreFloat4x4(&viewProjection, view * projection);
            model.SetModel(models[selectedModel]);
            model.SetTransform({ 0.0f, 0.0f, 0.0f }, { -0.3f, static_cast<float>(rotationY), 0.0f }, { 0.75f, 0.75f, 0.75f });
            secondModel.SetTransform({ 1.25f, -0.65f, 0.4f }, { 0.0f, -static_cast<float>(rotationY), 0.0f }, { 0.35f, 0.35f, 0.35f });
            model.Draw(commands, viewProjection, light, cameraPosition, modelUv);
            secondModel.Draw(commands, viewProjection, light, cameraPosition, modelUv);
            Engine::SpriteDrawParameters spriteParameters;
            spriteParameters.position = { static_cast<float>(renderer.GetWidth()) - 192.0f, 32.0f };
            spriteParameters.size = { 160.0f, 160.0f };
            spriteParameters.color = { 1.0f, 1.0f, 1.0f, 0.8f };
            spriteParameters.uvTransform = spriteUv;
            sprite.Draw(commands, renderer.GetWidth(), renderer.GetHeight(), spriteParameters);
            // 同じ画像を共有し、別の位置・UV 範囲で描画します。
            spriteParameters.position[0] -= 144.0f;
            spriteParameters.size = { 128.0f, 128.0f };
            spriteParameters.uvRect = { 0.0f, 0.0f, 0.5f, 0.5f };
            croppedSprite.Draw(commands, renderer.GetWidth(), renderer.GetHeight(), spriteParameters);
        }, debugUi);
    };

    Engine::Application application;
    const int exitCode = application.Run(settings, callbacks);
    gamepad.StopVibration();
    return exitCode;
}
