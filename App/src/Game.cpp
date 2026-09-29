#include "Game.h"
#include "Scenes/SceneFactory.h"
#include <Engine/Scenes/SceneManager.h>
#include <Engine/Core/Application.h>
#include <Windows.h>

namespace App
{
    int Game::Run()
    {
        std::wstring executable(32768, L'\0');
        const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (length == 0 || length >= executable.size()) return 1;
        executable.resize(length);
        SceneFactory factory(std::filesystem::path(executable).parent_path());
        Engine::SceneManager scenes(factory);
        scenes.RequestChange("Title");
        Engine::ApplicationSettings settings;
        settings.title = L"WP1 - Enter: Game / Escape: Title";
        Engine::ApplicationCallbacks callbacks;
        callbacks.update = [&](double deltaSeconds, const Engine::Keyboard& keyboard) { scenes.Update(deltaSeconds, keyboard); };
        callbacks.draw = [&](Engine::DirectX12Renderer& renderer) { return scenes.Draw(renderer); };
        Engine::Application application;
        // Run 内で GPU 完了を待った後に scenes が破棄されます。
        return application.Run(settings, callbacks);
    }
}
