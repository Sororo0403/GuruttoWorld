#include <Engine/Core/Application.h>
#include <Engine/Graphics/DirectX12Renderer.h>
#include <Engine/Graphics/SphereRenderer.h>
#include <Engine/Graphics/SpriteRenderer.h>
#include <Windows.h>
#if defined(_DEBUG)
#include "DebugPanel.h"
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
    const auto shaderPath = std::filesystem::path(executable).parent_path() / "Shaders" / "Sphere.hlsl";

    const auto texturePath = std::filesystem::path(executable).parent_path() / "Assets" / "Textures" / "Checker.png";

    const auto spriteShaderPath = std::filesystem::path(executable).parent_path() / "Shaders" / "Sprite.hlsl";

    Engine::ApplicationSettings settings;
    settings.title = L"WP1";
    settings.width = 1280;
    settings.height = 720;

    // Run が GPU の完了を待ってから戻るため、描画リソースはその後で安全に破棄できます。
    Engine::SphereRenderer sphere;
    bool sphereReady = false;
    Engine::SpriteRenderer sprite;
    bool spriteReady = false;
    double rotationY = 0.0;
    float speedDegrees = 90.0f;
    bool rotating = true;
    std::array<float, 4> backgroundColor{ 0.08f, 0.20f, 0.40f, 1.0f };
    std::function<void()> debugUi;
#if defined(_DEBUG)
    debugUi = [&]()
    {
        App::DebugPanel::Draw(rotationY, speedDegrees, rotating, backgroundColor);
    };
#endif
    Engine::ApplicationCallbacks callbacks;
    callbacks.update = [&](double deltaSeconds)
    {
        if (rotating)
        {
            const double angularSpeed = static_cast<double>(speedDegrees) * std::numbers::pi / 180.0;
            rotationY = std::fmod(rotationY + angularSpeed * deltaSeconds, 2.0 * std::numbers::pi);
        }
    };
    callbacks.draw = [&](Engine::DirectX12Renderer& renderer)
    {
        if (!sphereReady)
        {
            if (!sphere.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), texturePath, shaderPath))
            {
                return Engine::RenderResult::Failed;
            }
            sphereReady = true;
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
            const XMMATRIX view = XMMatrixLookAtLH(XMVectorSet(0.0f, 0.0f, -3.5f, 1.0f),
                XMVectorZero(), XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
            const XMMATRIX projection = XMMatrixPerspectiveFovLH(XM_PIDIV4, aspectRatio, 0.1f, 100.0f);
            XMFLOAT4X4 world;
            XMFLOAT4X4 viewProjection;
            XMStoreFloat4x4(&world, XMMatrixRotationY(static_cast<float>(rotationY)));
            XMStoreFloat4x4(&viewProjection, view * projection);
            sphere.Draw(commands, world, viewProjection);
            Engine::SpriteDrawParameters spriteParameters;
            spriteParameters.position = { static_cast<float>(renderer.GetWidth()) - 192.0f, 32.0f };
            spriteParameters.size = { 160.0f, 160.0f };
            spriteParameters.color = { 1.0f, 1.0f, 1.0f, 0.8f };
            sprite.Draw(commands, renderer.GetWidth(), renderer.GetHeight(), spriteParameters);
        }, debugUi);
    };

    Engine::Application application;
    return application.Run(settings, callbacks);
}
