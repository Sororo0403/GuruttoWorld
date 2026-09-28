#include <Engine/Core/Application.h>
#include <Engine/Graphics/DirectX12Renderer.h>
#include <Engine/Graphics/TriangleRenderer.h>
#include <Windows.h>

#include <filesystem>
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

    Engine::ApplicationSettings settings;
    settings.title = L"WP1";
    settings.width = 1280;
    settings.height = 720;

    // Run が GPU の完了を待ってから戻るため、三角形のリソースはその後で安全に破棄できます。
    Engine::TriangleRenderer triangle;
    bool triangleReady = false;
    Engine::ApplicationCallbacks callbacks;
    callbacks.draw = [&](Engine::DirectX12Renderer& renderer)
    {
        if (!triangleReady)
        {
            const std::array<Engine::TriangleVertex, 3> vertices =
            {{
                { { 0.0f, 0.65f, 0.0f }, { 1.0f, 0.15f, 0.15f, 1.0f } },
                { { 0.65f, -0.55f, 0.0f }, { 0.15f, 1.0f, 0.15f, 1.0f } },
                { { -0.65f, -0.55f, 0.0f }, { 0.15f, 0.3f, 1.0f, 1.0f } }
            }};
            if (!triangle.Initialize(renderer.GetDevice(), shaderPath, vertices))
            {
                return Engine::RenderResult::Failed;
            }
            triangleReady = true;
        }
        constexpr std::array<float, 4> backgroundColor{ 0.08f, 0.20f, 0.40f, 1.0f };
        return renderer.Render(backgroundColor, [&](ID3D12GraphicsCommandList* commands, float aspectRatio)
        {
            triangle.Draw(commands, aspectRatio);
        });
    };

    Engine::Application application;
    return application.Run(settings, callbacks);
}
