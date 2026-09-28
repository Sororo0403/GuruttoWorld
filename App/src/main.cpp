#include <Engine/Core/Application.h>
#include <Engine/Graphics/DirectX12Renderer.h>
#include <Engine/Graphics/TriangleRenderer.h>
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
    const auto shaderPath = std::filesystem::path(executable).parent_path() / "Shaders" / "Triangle.hlsl";

    const auto texturePath = std::filesystem::path(executable).parent_path() / "Assets" / "Textures" / "Checker.png";

    Engine::ApplicationSettings settings;
    settings.title = L"WP1";
    settings.width = 1280;
    settings.height = 720;

    // Run が GPU の完了を待ってから戻るため、三角形のリソースはその後で安全に破棄できます。
    Engine::TriangleRenderer triangle;
    bool triangleReady = false;
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
        if (!triangleReady)
        {
            const std::array<Engine::TriangleVertex, 3> vertices =
            {{
                { { 0.0f, 0.65f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.5f, 0.0f } },
                { { 0.65f, -0.55f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f } },
                { { -0.65f, -0.55f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f } }
            }};
            if (!triangle.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), texturePath, shaderPath, vertices))
            {
                return Engine::RenderResult::Failed;
            }
            triangleReady = true;
        }
        return renderer.Render(backgroundColor, [&](ID3D12GraphicsCommandList* commands, float aspectRatio)
        {
            triangle.Draw(commands, aspectRatio, static_cast<float>(rotationY), { -0.15f, 0.05f, -0.15f });
            // 奥の三角形を後から描いても、深度テストによって手前の面が残ります。
            triangle.Draw(commands, aspectRatio, 0.0f, { 0.2f, -0.1f, 0.25f }, { 1.0f, 0.45f, 0.2f, 1.0f });
        }, debugUi);
    };

    Engine::Application application;
    return application.Run(settings, callbacks);
}
