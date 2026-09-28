#include <Engine/Core/Application.h>
#include <Engine/Graphics/DirectX12Renderer.h>
#include <Windows.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    Engine::ApplicationSettings settings;
    settings.title = L"WP1";
    settings.width = 1280;
    settings.height = 720;

    Engine::ApplicationCallbacks callbacks;
    callbacks.draw = [](Engine::DirectX12Renderer& renderer)
    {
        constexpr std::array<float, 4> backgroundColor{ 0.08f, 0.20f, 0.40f, 1.0f };
        return renderer.Render(backgroundColor);
    };

    Engine::Application application;
    return application.Run(settings, callbacks);
}
