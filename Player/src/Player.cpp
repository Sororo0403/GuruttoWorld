#include <Engine/Assets/AssetArtifact.h>
#include <Player/Player.h>
#include "Scenes/SceneFactory.h"
#include <Engine/Scenes/SceneManager.h>
#include <Engine/Core/Application.h>
#include <SceneRuntime/Project/ProjectSettings.h>
#include <SceneRuntime/Scripts/ScriptModule.h>
#include <Windows.h>
#include <winrt/base.h>

namespace Player
{
    int Run(const Options& options)
    {
        Engine::AssetImportScope imports;
        std::wstring executable(32768, L'\0');
        const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (length == 0 || length >= executable.size())
            return 1;
        executable.resize(length);
        const auto root = std::filesystem::path(executable).parent_path();
        SceneRuntime::ProjectSettings project;
        try
        {
            project = SceneRuntime::ProjectSettings::Load(root);
            std::string error;
            if (!SceneRuntime::ScriptModule::Reload(root, error))
                throw std::runtime_error(error);
        }
        catch (const std::exception& exception)
        {
            MessageBoxW(nullptr, winrt::to_hstring(exception.what()).c_str(), L"プロジェクト設定エラー",
                        MB_OK | MB_ICONERROR);
            return 1;
        }
        SceneFactory factory(root, options.simulateInactive, options.playbackFeature);
        Engine::SceneManager scenes(factory);
        scenes.RequestChange(options.scene.empty() ? project.startupScene : options.scene);
        Engine::ApplicationSettings settings;
        settings.title = winrt::to_hstring(project.title).c_str();
        settings.width = project.width;
        settings.height = project.height;
        Engine::ApplicationCallbacks callbacks;
        callbacks.update = [&](double seconds, const Engine::Keyboard& keyboard)
        {
            if (options.hooks.update)
                options.hooks.update(seconds, keyboard, scenes, factory.Environment());
            else
                scenes.Update(seconds, keyboard);
        };
        callbacks.draw = [&](Engine::DirectX12Renderer& renderer)
        {
            return options.hooks.draw ? options.hooks.draw(renderer, scenes, factory.Environment())
                                      : scenes.Draw(renderer);
        };
        callbacks.closeRequested = options.hooks.closeRequested;
        callbacks.shouldClose = options.hooks.shouldClose;
        Engine::Application application;
        // Run 内で GPU 完了を待った後に scenes が破棄されます。
        return application.Run(settings, callbacks);
    }
}
