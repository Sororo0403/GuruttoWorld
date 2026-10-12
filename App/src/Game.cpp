#include "Game.h"
#include "Scenes/SceneFactory.h"
#include <Engine/Scenes/SceneManager.h>
#include <Engine/Core/Application.h>
#include <Windows.h>
#include <SceneRuntime/ProjectSettings.h>
#include <SceneRuntime/ScriptModule.h>
#include <Engine/Core/Log.h>
#include <winrt/base.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <Engine/Input/Keyboard.h>
#include <chrono>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <locale>

namespace App
{
    int Game::Run(const GameProfileOptions* profile)
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
        SceneFactory factory(root, profile && profile->simulateInactive);
        Engine::SceneManager scenes(factory);
        scenes.RequestChange(profile && !profile->scene.empty() ? profile->scene : project.startupScene);
        Engine::ApplicationSettings settings;
        settings.title = winrt::to_hstring(project.title).c_str();
        settings.width=project.width; settings.height=project.height;
        Engine::ApplicationCallbacks callbacks;
        std::ostringstream samples;
        samples.imbue(std::locale::classic());
        samples << std::setprecision(9);
        bool finished = false;
        unsigned int frame = 0;
        double updateMilliseconds = 0, intervalMilliseconds = 0;
        bool inputActive = false;
        using Clock = std::chrono::steady_clock;
        auto previousUpdate = Clock::now();
        callbacks.update = [&](double deltaSeconds, const Engine::Keyboard& keyboard) {
            if (!profile) { scenes.Update(deltaSeconds, keyboard); return; }
            const auto started = Clock::now();
            intervalMilliseconds = std::chrono::duration<double, std::milli>(started - previousUpdate).count();
            previousUpdate = started;
            inputActive = keyboard.IsActive();
            scenes.Update(deltaSeconds, keyboard);
            updateMilliseconds = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
        };
        callbacks.draw = [&](Engine::DirectX12Renderer& renderer) {
            if (!profile) return scenes.Draw(renderer);
            const auto started = Clock::now();
            const auto result = scenes.Draw(renderer);
            const double drawMilliseconds = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
            if (result == Engine::RenderResult::Presented) {
                const auto& telemetry = renderer.Telemetry();
                auto row = [&](const char* kind, const std::string& name, double milliseconds, unsigned int calls) {
                    samples << frame << ',' << scenes.GetActiveName() << ',' << renderer.GetWidth() << ',' << renderer.GetHeight()
                        << ',' << kind << ',' << name << ',' << milliseconds << ',' << calls << '\n';
                };
                row("frame", "interval", intervalMilliseconds, 1);
                row("state", "input.active", inputActive ? 1.0 : 0.0, 1);
                row("state", "simulation.override", profile->simulateInactive ? 1.0 : 0.0, 1);
                row("cpu", "app.update", updateMilliseconds, 1);
                row("cpu", "app.draw", drawMilliseconds, 1);
                row("cpu", "renderer.total", telemetry.cpuRenderMilliseconds, 1);
                for (const auto& sample : telemetry.cpuScopes) row("cpu.scope", sample.name, sample.milliseconds, sample.calls);
                // GPU値は再利用するバックバッファーの完了済みフレームに対応する。
                if (telemetry.gpuSample) {
                    row("gpu.delayed", "renderer.total", telemetry.gpuMilliseconds, 1);
                    for (const auto& sample : telemetry.gpuPasses) row("gpu.pass.delayed", sample.name, sample.milliseconds, sample.calls);
                }
                if (++frame >= profile->frames) finished = true;
            }
            return result;
        };
        if (profile) {
            callbacks.closeRequested = [&] { finished = true; };
            callbacks.shouldClose = [&] { return finished; };
        }
        Engine::Application application;
        // Run 内で GPU 完了を待った後に scenes が破棄されます。
        const int result = application.Run(settings, callbacks);
        if (profile) {
            try {
                if (profile->output.empty()) return 1;
                if (!profile->output.parent_path().empty()) std::filesystem::create_directories(profile->output.parent_path());
                std::ofstream output(profile->output, std::ios::binary);
                output << "frame,scene,width,height,kind,name,milliseconds,calls\n" << samples.str();
                if (!output) return 1;
            } catch (...) { return 1; }
            // 自動計測の通常終了はWM_QUITを経由しないため、取得フレーム数で判定する。
            return frame >= profile->frames ? 0 : (result == 0 ? 1 : result);
        }
        return result;
    }
}
