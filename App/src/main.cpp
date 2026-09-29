#include <Engine/Core/Application.h>
#include <Engine/Audio/AudioSystem.h>
#include <Engine/Input/Keyboard.h>
#include <Engine/Input/Gamepad.h>
#include <Engine/Graphics/DirectX12Renderer.h>
#include <Engine/Graphics/ModelRenderer.h>
#include <Engine/Graphics/SpriteRenderer.h>
#include <Windows.h>
#if defined(_DEBUG)
#include "DebugPanel.h"
#include "AudioPanel.h"
#include "InputPanel.h"
#include <imgui.h>
#include "LightingPanel.h"
#include "UVTransformPanel.h"
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
    settings.width = 1280;
    settings.height = 720;

    Engine::AudioSystem audio;
    Engine::SoundHandle sound = 0;
    bool audioAttempted = false;
    Engine::Gamepad gamepad;
    // Run が GPU の完了を待ってから戻るため、描画リソースはその後で安全に破棄できます。
    Engine::ModelRenderer model;
    bool modelReady = false;
    Engine::SpriteRenderer sprite;
    bool spriteReady = false;
    double rotationY = 0.0;
    float speedDegrees = 90.0f;
    bool rotating = true;
    std::array<float, 4> backgroundColor{ 0.08f, 0.20f, 0.40f, 1.0f };
    Engine::DirectionalLight light;
    Engine::UVTransform modelUV;
    Engine::UVTransform spriteUV;
    const std::array<float, 3> cameraPosition{ 0.0f, 0.0f, -3.5f };
    std::function<void()> debugUi;
#if defined(_DEBUG)
    debugUi = [&]()
    {
        App::AudioPanel::Draw(audio, sound);
        App::InputPanel::Draw(gamepad);
        App::DebugPanel::Draw(rotationY, speedDegrees, rotating, backgroundColor);
        App::LightingPanel::Draw(light);
        App::UVTransformPanel::Draw(modelUV, spriteUV);
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
        // スペースキーまたはコントローラーの A ボタンで再生します。
        bool captureKeyboard = false;
#if defined(_DEBUG)
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
            if (!model.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), modelPath, shaderPath))
            {
                return Engine::RenderResult::Failed;
            }
            modelReady = true;
        }
        if (!spriteReady)
        {
            if (!sprite.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), texturePath, spriteShaderPath))
            {
                return Engine::RenderResult::Failed;
            }
            spriteReady = true;
        }
        return renderer.Render(backgroundColor, [&](ID3D12GraphicsCommandList* commands, float aspectRatio)
        {
            using namespace DirectX;
            const XMMATRIX view = XMMatrixLookAtLH(XMVectorSet(cameraPosition[0], cameraPosition[1], cameraPosition[2], 1.0f),
                XMVectorZero(), XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
            const XMMATRIX projection = XMMatrixPerspectiveFovLH(XM_PIDIV4, aspectRatio, 0.1f, 100.0f);
            XMFLOAT4X4 world;
            XMFLOAT4X4 viewProjection;
            XMStoreFloat4x4(&world, XMMatrixRotationX(-0.3f) * XMMatrixRotationY(static_cast<float>(rotationY)));
            XMStoreFloat4x4(&viewProjection, view * projection);
            model.Draw(commands, world, viewProjection, light, cameraPosition, modelUV);
            Engine::SpriteDrawParameters spriteParameters;
            spriteParameters.position = { static_cast<float>(renderer.GetWidth()) - 192.0f, 32.0f };
            spriteParameters.size = { 160.0f, 160.0f };
            spriteParameters.color = { 1.0f, 1.0f, 1.0f, 0.8f };
            spriteParameters.uvTransform = spriteUV;
            sprite.Draw(commands, renderer.GetWidth(), renderer.GetHeight(), spriteParameters);
        }, debugUi);
    };

    Engine::Application application;
    const int exitCode = application.Run(settings, callbacks);
    gamepad.StopVibration();
    return exitCode;
}
