#include "Game.h"
#include "Scenes/SceneFactory.h"
#include <Engine/Scenes/SceneManager.h>
#include <Engine/Core/Application.h>
#include <Windows.h>
#include <SceneRuntime/ProjectSettings.h>
#include <SceneRuntime/ScriptModule.h>
#include <Engine/Core/Log.h>
#include <winrt/base.h>

namespace App
{
    int Game::Run()
    {
        std::wstring executable(32768, L'\0');
        const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (length == 0 || length >= executable.size()) return 1;
        executable.resize(length);
        const auto root=std::filesystem::path(executable).parent_path();
        SceneRuntime::ProjectSettings project;
        try {
            project=SceneRuntime::ProjectSettings::Load(root);
            std::string error; if (!SceneRuntime::ScriptModule::Reload(root,error)) throw std::runtime_error(error);
        }
        catch(const std::exception& exception) {
            MessageBoxW(nullptr,winrt::to_hstring(exception.what()).c_str(),L"プロジェクト設定エラー",MB_OK|MB_ICONERROR);
            return 1;
        }
        SceneFactory factory(root);
        Engine::SceneManager scenes(factory);
        scenes.RequestChange(project.startupScene);
        Engine::ApplicationSettings settings;
        settings.title = winrt::to_hstring(project.title).c_str();
        settings.width=project.width; settings.height=project.height;
        Engine::ApplicationCallbacks callbacks;
        callbacks.update = [&](double deltaSeconds, const Engine::Keyboard& keyboard) { scenes.Update(deltaSeconds, keyboard); };
        callbacks.draw = [&](Engine::DirectX12Renderer& renderer) { return scenes.Draw(renderer); };
        Engine::Application application;
        // Run 内で GPU 完了を待った後に scenes が破棄されます。
        return application.Run(settings, callbacks);
    }
}
