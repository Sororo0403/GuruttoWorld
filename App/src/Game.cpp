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
        bool inputActive = false, deviceActive = false;
        Engine::Keyboard replayKeyboard;
        bool jumpDown = false, jumpPressed = false, jumpReleased = false;
        unsigned int inputFrame = 0;
        std::string inputEvent = "none";
        double replaySeconds = 0;
        using Clock = std::chrono::steady_clock;
        auto previousUpdate = Clock::now();
        callbacks.update = [&](double deltaSeconds, const Engine::Keyboard& keyboard) {
            if (!profile) { scenes.Update(deltaSeconds, keyboard); return; }
            const auto started = Clock::now();
            intervalMilliseconds = std::chrono::duration<double, std::milli>(started - previousUpdate).count();
            previousUpdate = started;
            deviceActive = keyboard.IsActive();
            if (profile->replayInput) {
                std::array<BYTE,256> keys{};
                const auto tick = inputFrame++;
                const bool active = !(tick >= 210 && tick < 230);
                Engine::DiagnosticPointer pointer;
                pointer.x = 220; pointer.y = 540;
                inputEvent = "idle";
                if (tick >= 30 && tick < 54) { keys[DIK_A]=0x80; inputEvent="move.left"; }
                if (tick >= 54 && tick < 180) { keys[DIK_W]=0x80; inputEvent="move.forward"; }
                if (tick == 130 || tick == 160 || tick == 250) { keys[DIK_SPACE]=0x80; inputEvent="jump.press"; }
                if (tick == 170 || tick == 270) { keys[DIK_J]=0x80; inputEvent="attack.press"; }
                if (tick >= 190 && tick < 195) { pointer.buttons=1; inputEvent="pointer.hold"; }
                if (tick == 195) inputEvent="pointer.release";
                if (tick == 200) { pointer.wheel=1; inputEvent="pointer.wheel"; }
                if (!active) { keys[DIK_SPACE]=0x80; inputEvent="focus.lost"; }
                if (tick == 230) { keys[DIK_SPACE]=0x80; inputEvent="focus.regained.held"; }
                replayKeyboard.ApplyDiagnosticState(keyboard.WindowHandle(),active,keys,pointer);
                inputActive = replayKeyboard.IsActive();
                replaySeconds += 1.0/60.0;
                scenes.Update(1.0/60.0,replayKeyboard);
            } else {
                inputActive = deviceActive;
                scenes.Update(deltaSeconds, keyboard);
            }
            const auto& measuredKeyboard = profile->replayInput ? replayKeyboard : keyboard;
            jumpDown = measuredKeyboard.IsDown(DIK_SPACE);
            jumpPressed = measuredKeyboard.IsPressed(DIK_SPACE);
            jumpReleased = measuredKeyboard.IsReleased(DIK_SPACE);
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
                row("state", "input.device.active", deviceActive ? 1.0 : 0.0, 1);
                row("state", "input.diagnostic.replay", profile->replayInput ? 1.0 : 0.0, 1);
                if (profile->replayInput) {
                    row("event", inputEvent, replaySeconds, 1);
                    row("state", "replay.update.index", inputFrame-1, 1);
                    row("state", "replay.jump.pressed", replayKeyboard.IsPressed(DIK_SPACE) ? 1.0 : 0.0, 1);
                    row("state", "replay.jump.released", replayKeyboard.IsReleased(DIK_SPACE) ? 1.0 : 0.0, 1);
                }
                unsigned int collectibles=0;
                for (const auto& object : factory.Environment().World().Layout().objects) {
                    if (object.id.find("-coin-")!=std::string::npos) ++collectibles;
                    if (object.id=="park-player") {
                        row("state", "player.x", object.position[0], 1);
                        row("state", "player.y", object.position[1], 1);
                        row("state", "player.z", object.position[2], 1);
                    }
                }
                row("state", "collectibles.remaining", collectibles, 1);
                row("state", "ui.hovered", factory.Environment().Ui().hovered.empty() ? 0 : 1, 1);
                row("state", "ui.pressed", factory.Environment().Ui().pressed.empty() ? 0 : 1, 1);
                row("state", "ui.focused", factory.Environment().Ui().focused.empty() ? 0 : 1, 1);
                row("state", "input.jump.down", jumpDown ? 1.0 : 0.0, 1);
                row("state", "input.jump.pressed", jumpPressed ? 1.0 : 0.0, 1);
                row("state", "input.jump.released", jumpReleased ? 1.0 : 0.0, 1);
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
