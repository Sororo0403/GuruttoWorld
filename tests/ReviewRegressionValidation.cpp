#include "../Editor/src/TransformMatrix.h"
#include "../Editor/src/EditHistory.h"
#if defined(_DEBUG)
#include <Engine/DevTools/DebugCamera.h>
#endif
#include <SceneRuntime/SceneLayout.h>
#include <SceneRuntime/SceneWorld.h>
#include <Engine/Core/DiagnosticPaths.h>
#include <Engine/Core/Log.h>
#include <Engine/Core/CrashHandler.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <Engine/Graphics/DirectX12/GpuSynchronization.h>
#include <Engine/Graphics/Renderers/MeshRenderer.h>
#include <Engine/Graphics/Resources/DepthBuffer.h>
#include "../App/src/Scenes/TitleScene.h"
#include "../App/src/Scenes/TitleMenu.h"
#include <ShlObj.h>
#include <d3d12sdklayers.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace
{
    using Microsoft::WRL::ComPtr;
    void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
    void Hr(HRESULT result) { Check(SUCCEEDED(result), "D3D12 call failed"); }

    void CheckGpuMessages(ID3D12Device* device)
    {
        ComPtr<ID3D12InfoQueue> messages;
        if (FAILED(device->QueryInterface(IID_PPV_ARGS(&messages)))) return;
        for (UINT64 i = 0; i < messages->GetNumStoredMessages(); ++i)
        {
            SIZE_T size = 0;
            Hr(messages->GetMessage(i, nullptr, &size));
            std::vector<unsigned char> storage(size);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            Hr(messages->GetMessage(i, message, &size));
            Check(message->Severity > D3D12_MESSAGE_SEVERITY_ERROR, message->pDescription);
        }
    }

    void ValidateDiagnostics()
    {
        PWSTR directory = nullptr;
        Hr(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &directory));
        const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owner(directory, CoTaskMemFree);
        Check(Engine::GetDiagnosticsRoot() == std::filesystem::path(directory) / "WP1", "user diagnostics path");
        // ファイルを作る検証では明示パスを使い、ユーザーの診断ファイルを変更しません。
        const auto folder = std::filesystem::path("generated/tests/diagnostics") / std::to_string(GetTickCount64());
        const auto log = folder / "custom.log";
        Check(Engine::Log::Initialize(log), "custom log path");
        Engine::Log::Info("diagnostic regression marker");
        Engine::Log::Shutdown();
        std::ifstream stream(log);
        const std::string contents{ std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
        Check(contents.find("diagnostic regression marker") != std::string::npos, "custom log output");
        Check(Engine::CrashHandler::Initialize(folder / "crashes"), "custom crash path");
        Engine::CrashHandler::Shutdown();
        Check(std::filesystem::is_directory(folder / "crashes"), "crash directory created");
    }

    void ValidateTitleMenu()
    {
        using namespace App;
        TitleMenu menu;
        TitleMenuInput input;
        input.active = true;
        input.keyboardButtons = MenuConfirm;
        Check(menu.Update(input) == TitleMenuAction::None, "suppress confirm held on entry");
        input.keyboardButtons = 0;
        menu.Update(input);
        input.keyboardButtons = MenuUp;
        Check(menu.Update(input) == TitleMenuAction::None && menu.GetSelected() == TitleMenuItem::Exit, "wrap to exit");
        menu.Update(input);
        Check(menu.GetSelected() == TitleMenuItem::Exit, "held navigation does not repeat");
        input.active = false;
        input.keyboardButtons = MenuConfirm;
        Check(menu.Update(input) == TitleMenuAction::None, "inactive confirm ignored");
        input.active = true;
        Check(menu.Update(input) == TitleMenuAction::None, "suppress confirm held on refocus");
        input.keyboardButtons = 0;
        menu.Update(input);
        input.keyboardButtons = MenuConfirm;
        Check(menu.Update(input) == TitleMenuAction::None, "exit begins transition");
        for (int i = 0; i < 4; ++i) Check(menu.Update(input, 0.1) == TitleMenuAction::None, "exit waits for cover");
        Check(menu.Update(input, 0.1) == TitleMenuAction::Exit, "exit selected");
        Check(menu.Update(input) == TitleMenuAction::None, "exit emitted only once");

        TitleMenu padMenu;
        input = {};
        input.active = true;
        padMenu.Update(input);
        input.gamepadConnected = true;
        input.gamepadButtons = MenuConfirm;
        input.stickY = -1.0f;
        Check(padMenu.Update(input) == TitleMenuAction::None && padMenu.GetSelected() == TitleMenuItem::Start, "suppress held input on connection");
        input.gamepadButtons = 0;
        input.stickY = 0;
        padMenu.Update(input);
        input.stickY = -0.8f;
        padMenu.Update(input);
        Check(padMenu.GetSelected() == TitleMenuItem::Settings && padMenu.UsesGamepad(), "stick selection and device hint");
        input.stickY = -0.4f;
        padMenu.Update(input);
        input.stickY = -0.6f;
        padMenu.Update(input);
        Check(padMenu.GetSelected() == TitleMenuItem::Settings, "stick hysteresis avoids repeated navigation");
        input.stickY = 0;
        padMenu.Update(input);
        input.keyboardButtons = MenuUp | MenuConfirm;
        Check(padMenu.Update(input) == TitleMenuAction::None && padMenu.GetSelected() == TitleMenuItem::Start, "move and confirm cannot start or exit together");
        Check(!padMenu.UsesGamepad(), "keyboard hint follows keyboard activity");
        input.keyboardButtons = 0;
        padMenu.Update(input);
        input.gamepadButtons = MenuConfirm;
        Check(padMenu.Update(input) == TitleMenuAction::None, "gamepad begins transition");
        for (int i = 0; i < 4; ++i) padMenu.Update(input, 0.1);
        Check(padMenu.Update(input, 0.1) == TitleMenuAction::Start, "gamepad start");

        TitleMenu directions;
        input = {};
        input.active = true;
        directions.Update(input);
        input.keyboardButtons = MenuUp | MenuDown;
        directions.Update(input);
        Check(directions.GetSelected() == TitleMenuItem::Start, "opposing directions cancel");
    }

    void ValidateTitleAnimation()
    {
        using namespace App;
        TitleMenu menu(true);
        TitleMenuInput input;
        input.active = true;
        menu.Update(input, 0.1);
        input.keyboardButtons = MenuConfirm;
        Check(menu.Update(input, 0.1) == TitleMenuAction::None && menu.IntroProgress() == 1.0f,
            "confirm skips intro without starting");
        Check(menu.Update(input, 0.1) == TitleMenuAction::None && menu.TransitionProgress() == 0,
            "held skip cannot start game");
        input.keyboardButtons = 0;
        menu.Update(input, 0.1);
        input.keyboardButtons = MenuConfirm;
        menu.Update(input, 0.1);
        input.active = false;
        menu.Update(input, 10.0);
        Check(menu.TransitionProgress() == 0, "inactive transition paused");
        input.active = true;
        for (int i = 0; i < 4; ++i)
            Check(menu.Update(input, 0.1) == TitleMenuAction::None, "transition holds scene until fully covered");
        Check(menu.TransitionProgress() == 1.0f, "transition fully covers screen");
        Check(menu.Update(input, 0.1) == TitleMenuAction::Start, "transition emits start after covered frame");
        Check(menu.Update(input, 0.1) == TitleMenuAction::None, "transition emitted once");
        TitleMenu returning(false);
        Check(returning.IntroProgress() == 1.0f, "revisit skips intro");
        input.keyboardButtons = 0;
        returning.Update(input);
        input.keyboardButtons = MenuDown;
        returning.Update(input);
        Check(returning.SelectionPulse() == 1.0f, "selection feedback starts immediately");
        returning.Update(input, 0.1);
        returning.Update(input, 0.1);
        Check(returning.SelectionPulse() == 0, "selection feedback settles");
        TitleMenu timed(true);
        timed.Update(input, -1.0);
        Check(timed.IntroProgress() == 0, "negative time ignored");
        for (int i = 0; i < 7; ++i) timed.Update(input, 0.1);
        Check(timed.IntroProgress() == 1.0f && timed.GetSelected() == TitleMenuItem::Start, "intro completes without accidental held navigation");
    }

    void ValidateSettings()
    {
        using namespace App;
        const auto folder = std::filesystem::path("generated/tests/settings") / std::to_string(GetTickCount64());
        const auto path = folder / "settings.txt";
        Check(GameSettings::UserPath() == Engine::GetDiagnosticsRoot() / "settings.txt", "user settings location");
        Check(GameSettings::Load(path).volume == 10, "missing settings defaults");
        GameSettings settings{ 3, false };
        Check(settings.Save(path), "save settings");
        const auto restored = GameSettings::Load(path);
        Check(restored.volume == 3 && !restored.backgroundMotion, "settings survive reload");
        Check(restored.SampleVolume() > 0.074f && restored.SampleVolume() < 0.076f, "sample audio multiplier");
        Check(GameSettings{ 0, true }.SampleVolume() == 0.0f, "mute sample audio");
        Check(GameSettings{ 7, true }.Save(path) && GameSettings::Load(path).volume == 7, "replace existing settings");
        Check(!settings.Save(path / "blocked.txt") && GameSettings::Load(path).volume == 7, "failed save preserves settings");
        for (const auto* invalid : { "", "WP1_SETTINGS 2 5 1", "WP1_SETTINGS 1 -1 1", "WP1_SETTINGS 1 11 1",
            "WP1_SETTINGS 1 3 2", "WP1_SETTINGS 1 nan 1", "WP1_SETTINGS 1 3", "WP1_SETTINGS 1 3 1 trailing" })
        {
            { std::ofstream stream(path); stream << invalid; }
            const auto fallback = GameSettings::Load(path);
            Check(fallback.volume == 10 && fallback.backgroundMotion, "invalid settings default safely");
        }
        TitleMenu menu;
        menu.LoadSettings(settings);
        TitleMenuInput input;
        input.active = true;
        menu.Update(input);
        const auto press = [&](unsigned int buttons)
        {
            input.keyboardButtons = 0;
            menu.Update(input);
            input.keyboardButtons = buttons;
            return menu.Update(input);
        };
        press(MenuDown);
        press(MenuConfirm);
        Check(menu.IsSettingsOpen(), "settings opens");
        Check(menu.Update(input) == TitleMenuAction::None && menu.GetSettingsRow() == 0, "held confirm cannot modify settings");
        for (int i = 0; i < 20; ++i) press(MenuLeft);
        Check(menu.GetSettings().volume == 0, "volume lower bound");
        for (int i = 0; i < 20; ++i) press(MenuRight);
        Check(menu.GetSettings().volume == 10, "volume upper bound");
        press(MenuBack);
        Check(!menu.IsSettingsOpen() && menu.GetSettings().volume == 3, "cancel restores saved settings");
        press(MenuConfirm);
        press(MenuLeft);
        press(MenuDown);
        press(MenuConfirm);
        Check(menu.GetSettings().backgroundMotion, "toggle background motion");
        press(MenuDown);
        Check(press(MenuConfirm) == TitleMenuAction::SaveSettings, "save requested");
        menu.CompleteSave(false);
        Check(menu.IsSettingsOpen() && menu.SaveFailed(), "save error remains visible");
        Check(press(MenuConfirm) == TitleMenuAction::SaveSettings, "retry save");
        menu.CompleteSave(true);
        Check(!menu.IsSettingsOpen() && !menu.SaveFailed(), "successful save closes settings");
        press(MenuConfirm);
        Check(menu.GetSettings().volume == 2 && menu.GetSettings().backgroundMotion, "reopening preserves saved values");
        input.keyboardButtons = 0;
        input.gamepadConnected = true;
        input.stickX = 1.0f;
        menu.Update(input);
        Check(menu.GetSettings().volume == 2, "suppress horizontal stick held on connection");
        input.stickX = 0;
        menu.Update(input);
        input.stickX = -1;
        menu.Update(input);
        Check(menu.GetSettings().volume == 1 && menu.UsesGamepad(), "gamepad adjusts volume");
        input.gamepadButtons = MenuBack;
        menu.Update(input);
        Check(!menu.IsSettingsOpen(), "gamepad back");
    }

    void ValidateAmbientMotion()
    {
        App::TitleMenu pressAny(true, true);
        App::TitleMenuInput startInput;
        startInput.active = true;
        startInput.anyButtonPressed = true;
        pressAny.Update(startInput, 0.1);
        Check(pressAny.TransitionProgress() == 0.0f, "first activation cannot start press-any title");
        startInput.anyButtonPressed = false;
        pressAny.Update(startInput, 0.1);
        startInput.anyButtonPressed = true;
        pressAny.Update(startInput, 0.1);
        Check(pressAny.GetCue() == App::TitleMenuCue::Confirm && !pressAny.IsSettingsOpen(),
            "any button starts directly even during intro");
        startInput.anyButtonPressed = false;
        for (int frame = 0; frame < 4; ++frame)
            Check(pressAny.Update(startInput, 0.1) == App::TitleMenuAction::None, "press-any transition draws before start");
        Check(pressAny.Update(startInput, 0.1) == App::TitleMenuAction::Start, "press-any emits game start");
        Check(pressAny.Update(startInput, 0.1) == App::TitleMenuAction::None, "press-any emits start once");
        App::TitleAmbientMotion motion;
        App::TitleAmbientMotion exitCamera;
        const auto home = exitCamera.CameraPosition();
        Engine::Camera framing;
        framing.SetPosition(home);
        framing.SetRotation(0.03f, 0.09f);
        framing.SetPerspective(DirectX::XM_PIDIV4, 16.0f / 9.0f, 0.1f, 220.0f);
        for (int frame = 0; frame < 7; ++frame) exitCamera.Update(0.1, false, true, false, true);
        Check(std::abs(exitCamera.CameraPosition()[2] - 20.0f) < 0.001f &&
            std::abs(exitCamera.CameraRotation()[0] - 0.56f) < 0.001f,
            "exit hover frames gate in the building row within 0.7 seconds");
        framing.SetPosition(exitCamera.CameraPosition());
        framing.SetRotation(exitCamera.CameraRotation()[0], exitCamera.CameraRotation()[1]);
        const auto gateCenter = DirectX::XMVector3TransformCoord(
            DirectX::XMVectorSet(4.8f, 3.0f, 26.0f, 1.0f),
            framing.GetViewMatrix() * framing.GetProjectionMatrix());
        Check(DirectX::XMVectorGetX(gateCenter) > 0.0f && DirectX::XMVectorGetX(gateCenter) < 0.8f,
            "exit hover reveals gate on right of menu");
        const auto exitPose = exitCamera.CameraPosition();
        exitCamera.Update(0.1, false, false, true, false);
        Check(exitCamera.CameraPosition() == exitPose, "inactive exit camera pauses");
        exitCamera.Update(0.1, false, true, true, false);
        Check(std::abs(exitCamera.CameraPosition()[2] - exitPose[2]) < 0.5f,
            "switching exit to settings starts smoothly");
        for (int frame = 0; frame < 20; ++frame)
        {
            exitCamera.Update(0.1, false, true, true, false);
            Check(exitCamera.CameraPosition()[2] >= 7.0f && exitCamera.CameraPosition()[2] <= 20.0f,
                "exit to settings travels directly without returning home or overshooting");
        }
        Check(exitCamera.CameraRotation()[0] < -0.8f, "exit to settings turns toward control facility");
        exitCamera.Update(0.1, false, true);
        const auto interruptedPosition = exitCamera.CameraPosition();
        const auto interruptedRotation = exitCamera.CameraRotation();
        exitCamera.Update(0.0, false, true, false, true);
        Check(exitCamera.CameraPosition() == interruptedPosition && exitCamera.CameraRotation() == interruptedRotation,
            "mid-flight retarget starts at current pose without snapping");
        for (int frame = 0; frame < 7; ++frame) exitCamera.Update(0.1, false, true, false, true);
        Check(std::abs(exitCamera.CameraPosition()[2] - 20.0f) < 0.001f &&
            std::abs(exitCamera.CameraRotation()[0] - 0.56f) < 0.001f, "retarget reaches exit endpoint");
        for (int frame = 0; frame < 20; ++frame) exitCamera.Update(0.1, false, true);
        Check(exitCamera.CameraPosition() == home, "leaving menu targets restores home camera");
        const auto original = motion.CameraPosition();
        motion.Update(0.1, true, true);
        Check(motion.CameraPosition() != original, "ambient camera advances");
        const auto paused = motion.CameraPosition();
        const auto mote = motion.Mote(5);
        motion.Update(100.0, false, true);
        Check(!motion.IsEnabled() && motion.CameraPosition() == paused && motion.Mote(5) == mote, "OFF freezes phase and hides motes");
        motion.Update(100.0, true, false);
        Check(motion.CameraPosition() == paused && motion.Mote(5) == mote, "inactive background paused");
        motion.Update(-1.0, true, true);
        motion.Update(std::numeric_limits<double>::quiet_NaN(), true, true);
        Check(motion.CameraPosition() == paused, "invalid ambient time ignored");
        for (int frame = 0; frame < 2500; ++frame)
        {
            motion.Update(0.1, true, true);
            const auto position = motion.CameraPosition();
            Check(position[0] >= -0.901f && position[0] <= -0.699f &&
                position[1] >= 2.759f && position[1] <= 2.841f && position[2] == -7.0f, "camera stays within composition bounds");
            for (unsigned int index = 0; index < 24; ++index)
            {
                const auto value = motion.Mote(index);
                Check(std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]) &&
                    value[3] >= 0 && value[3] <= 0.321f, "bounded ambient particle opacity");
            }
        }
    }

    void ValidateTitleAudio()
    {
        using namespace App;
        TitleMenu menu;
        TitleMenuInput input;
        input.active = true;
        menu.Update(input);
        input.keyboardButtons = MenuDown;
        menu.Update(input);
        Check(menu.GetCue() == TitleMenuCue::Select, "selection cue");
        menu.Update(input);
        Check(menu.GetCue() == TitleMenuCue::None, "held input does not repeat cue");
        input.keyboardButtons = MenuConfirm;
        menu.Update(input);
        Check(menu.GetCue() == TitleMenuCue::Confirm, "settings open cue");
        menu.CompleteSave(false);
        Check(menu.GetCue() == TitleMenuCue::Error, "failed save error cue");
        input.keyboardButtons = MenuBack;
        menu.Update(input);
        Check(menu.GetCue() == TitleMenuCue::Back, "cancel cue");
        input.active = false;
        menu.Update(input);
        Check(menu.GetCue() == TitleMenuCue::None, "inactive silence");
        TitleMenu intro(true);
        input.active = true;
        input.keyboardButtons = 0;
        intro.Update(input);
        input.keyboardButtons = MenuConfirm;
        intro.Update(input);
        Check(intro.GetCue() == TitleMenuCue::None, "intro skip does not play confirmation");
        Engine::AudioSystem audio;
        Check(audio.Initialize(), "title audio device");
        for (const auto* file : { "Bgm.wav", "Select.wav", "Confirm.wav", "Back.wav", "Error.wav" })
        {
            const auto sound = audio.Load(std::filesystem::path("App/Assets/Audio/Title") / file);
            Check(sound != 0, "title sound decoding");
            Check(audio.SetVolume(sound, 0.0f) && audio.GetVolume(sound) == 0.0f, "title sound mute");
            Check(audio.Play(sound, true) && audio.IsPlaying(sound), "title sound loop playback silently");
            audio.Stop(sound);
            Check(!audio.IsPlaying(sound), "title sound stop");
            audio.Unload(sound);
        }
    }

    std::filesystem::path TestContentRoot()
    {
        std::wstring executable(32768, L'\0');
        const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        Check(length > 0 && length < executable.size(), "test executable path");
        executable.resize(length);
        return std::filesystem::path(executable).parent_path();
    }

    void ValidateTitle()
    {
        Check(Engine::Log::Initialize("generated/tests/title-rendering.log"), "title diagnostic log");
        constexpr std::array<std::array<int, 2>, 3> TitleSizes{{ {1280, 720}, {1024, 768}, {720, 1280} }};
        for (const auto& size : TitleSizes)
        {
            Engine::Window window;
            Engine::DirectX12Renderer renderer;
            Check(window.Create(L"Hidden title validation", size[0], size[1]), "title window");
            Check(renderer.Initialize(window.GetHandle()), "title renderer");
            {
                SceneRuntime::SceneWorld editorWorld;
                const auto content = std::filesystem::absolute("Content");
                Check(editorWorld.Initialize(renderer, content, content / "Assets/Scenes/TitleStreet.json",
                    content / "Shaders/TitleMesh.hlsl"), "shared scene loads without App content");
                const auto pickRoot = std::filesystem::absolute("generated/tests/picking");
                std::filesystem::create_directories(pickRoot / "Assets/Models/Title");
                {
                    std::ofstream fixture(pickRoot / "Assets/Models/Title/triangle.obj");
                    fixture << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
                }
                SceneRuntime::SceneLayout pickLayout;
                SceneRuntime::ScenePlacement nearObject;
                nearObject.id = "pick-near";
                nearObject.name = "Near triangle";
                nearObject.model = "Assets/Models/Title/triangle.obj";
                nearObject.position = { 0, 0, 5 };
                nearObject.scale = { -2, 3, 2 };
                auto farObject = nearObject;
                farObject.id = "pick-far";
                farObject.position[2] = 10;
                pickLayout.objects = { farObject, nearObject };
                const auto pickPath = pickRoot / "scene.json";
                pickLayout.Save(pickPath);
                SceneRuntime::SceneWorld pickingWorld;
                Check(pickingWorld.Initialize(renderer, pickRoot, pickPath, content / "Shaders/TitleMesh.hlsl"),
                    "picking fixture loaded");
                Check(pickingWorld.PickRay({ -0.4f, 0.6f, 0 }, { 0, 0, 5 }).value_or("") == "pick-near",
                    "nearest triangle selected independent of object order and mirrored scale");
                Check(!pickingWorld.PickRay({ -1.8f, 2.7f, 0 }, { 0, 0, 1 }),
                    "empty portion of bounding box is not selected");
                Check(!pickingWorld.PickRay({ -0.4f, 0.6f, 0 }, { 0, 0, 1 }, 1) &&
                    !pickingWorld.PickRay({ 0, 0, 0 }, { 0, 0, 0 }) &&
                    !pickingWorld.PickRay({ NAN, 0, 0 }, { 0, 0, 1 }), "invalid and out-of-range rays rejected");
                std::array<std::array<float, 3>, 8> corners;
                Check(pickingWorld.WorldBounds("pick-near", corners), "selected world bounds available");
                std::array<float, 3> center{};
                for (const auto& corner : corners)
                    for (size_t axis = 0; axis < 3; ++axis) center[axis] += corner[axis] / 8;
                Check(std::abs(center[0] + 1) < 0.001f && std::abs(center[1] - 1.5f) < 0.001f &&
                    std::abs(center[2] - 5) < 0.001f, "selection corners use instance transform");
                Check(pickingWorld.SetTransform("pick-near", nearObject.position, { 0, DirectX::XM_PIDIV2, 0 },
                    nearObject.scale) && pickingWorld.PickRay({ -5, 0.6f, 5.4f }, { 1, 0, 0 }).value_or("") == "pick-near",
                    "rotated object uses updated picking transform");
                Check(pickingWorld.RemoveObject("pick-near") &&
                    pickingWorld.PickRay({ -0.4f, 0.6f, 0 }, { 0, 0, 1 }).value_or("") == "pick-far" &&
                    !pickingWorld.WorldBounds("pick-near", corners), "deleted object cannot be selected or outlined");
                Check(renderer.WaitForIdle(), "picking resources GPU completion");
                const auto original = editorWorld.Layout().objects.front();
                auto moved = original.position;
                moved[2] += 2.0f;
                const std::array<float, 3> rotated{ 0.1f, 0.2f, 0.3f };
                const std::array<float, 3> scaled{ 120, 4, 160 };
                Check(editorWorld.SetTransform(original.id, moved, rotated, scaled), "live transform edit accepted");
                const auto& changed = editorWorld.Layout().objects.front();
                Check(changed.position == moved && changed.rotation == rotated && changed.scale == scaled &&
                    changed.model == original.model, "live edits update layout while keeping model reference");
                Check(!editorWorld.SetTransform(original.id, original.position, original.rotation, { 0, 4, 4 }),
                    "zero scale edit rejected");
                auto invalidPosition = original.position;
                invalidPosition[0] = NAN;
                Check(!editorWorld.SetTransform(original.id, invalidPosition, original.rotation, original.scale),
                    "nonfinite position edit rejected");
                Check(!editorWorld.SetTransform("missing-object", original.position, original.rotation, original.scale),
                    "unknown object edit rejected");
                Check(changed.position == moved && changed.rotation == rotated && changed.scale == scaled,
                    "rejected edits preserve previous valid placement");
                Engine::Camera editorView;
                editorView.SetPosition({ -0.8f, 2.8f, -7.0f });
                editorView.SetRotation(0.03f, 0.09f);
                editorView.SetPerspective(DirectX::XM_PIDIV4, float(size[0]) / float(size[1]), 0.1f, 220.0f);
                Check(renderer.Render({ 0.66f, 0.79f, 0.83f, 1 }, [&](ID3D12GraphicsCommandList* commands, float)
                {
                    editorWorld.Draw(commands, editorView, {});
                }) != Engine::RenderResult::Failed, "independent editor scene rendering");
                Check(renderer.WaitForIdle(), "editor scene GPU completion");
                SceneRuntime::SceneWorld startupFailure;
                std::string startupError;
                Check(!startupFailure.Initialize(renderer, content, content / "Assets/Scenes/missing.json",
                    content / "Shaders/TitleMesh.hlsl", &startupError) && !startupError.empty(),
                    "startup load failure provides a diagnostic");
                Check(startupFailure.Reload(content, content / "Assets/Scenes/TitleStreet.json", startupError),
                    "failed initial load can be recovered by reload");
                Check(renderer.WaitForIdle(), "startup recovery GPU completion");
                std::string reloadError;
                Check(!editorWorld.Reload(content, content / "Assets/Scenes/missing.json", reloadError) &&
                    !reloadError.empty() && editorWorld.Layout().objects.front().position == moved,
                    "failed reload preserves live edits and provides error");
                auto invalidLayout = editorWorld.Layout();
                invalidLayout.objects.back().model = "Assets/Models/Title/Roads/missing.obj";
                const auto reloadPath = std::filesystem::absolute("generated/tests/layout-io/missing-model.json");
                invalidLayout.Save(reloadPath);
                Check(!editorWorld.Reload(content, reloadPath, reloadError) &&
                    editorWorld.Layout().objects.front().position == moved,
                    "partial model load failure preserves current world");
                Check(editorWorld.Reload(content, content / "Assets/Scenes/TitleStreet.json", reloadError) &&
                    reloadError.empty() && editorWorld.Layout().objects.front().position == original.position,
                    "successful reload restores saved scene");
                SceneRuntime::ScenePlacement added;
                added.name = "Test awning";
                added.model = "Assets/Models/Title/Commercial/detail-awning.obj";
                added.position = { 9, 0.16f, 10 };
                added.rotation = { 0, 0.25f, 0 };
                added.scale = { 4, 4, 4 };
                const auto count = editorWorld.Layout().objects.size();
                std::string addedId, duplicateId, operationError;
                Check(editorWorld.AddObject(added, content, addedId, operationError) && !addedId.empty(),
                    "new model can be added with a generated ID");
                Check(editorWorld.DuplicateObject(addedId, { 4, 0, 0 }, duplicateId, operationError) &&
                    duplicateId != addedId && editorWorld.Layout().objects.back().position[0] == 13 &&
                    editorWorld.Layout().objects.back().rotation == added.rotation &&
                    editorWorld.Layout().objects.back().scale == added.scale,
                    "duplicate preserves model transform and has a distinct ID");
                added.id = addedId;
                std::string rejectedId;
                Check(!editorWorld.AddObject(added, content, rejectedId, operationError), "duplicate ID rejected");
                added.id.clear();
                added.model = "Assets/Models/Title/Roads/missing.obj";
                Check(!editorWorld.AddObject(added, content, rejectedId, operationError) &&
                    editorWorld.Layout().objects.size() == count + 2, "failed addition keeps all current objects");
                Check(!editorWorld.DuplicateObject(addedId, { NAN, 0, 0 }, rejectedId, operationError) &&
                    editorWorld.Layout().objects.size() == count + 2, "invalid duplicate keeps current scene");
                Check(!editorWorld.RemoveObject("missing-id"), "unknown delete does not change scene");
                const auto beforeDelete=editorWorld.Layout();
                Check(editorWorld.RemoveObject(addedId) && editorWorld.Layout().objects.size() == count + 1 &&
                    editorWorld.Layout().objects.back().id == duplicateId, "delete removes only selected object");
                const auto afterDelete=editorWorld.Layout();
                Check(editorWorld.ReplaceLayout(beforeDelete, content, operationError) &&
                    editorWorld.Layout().Serialize()==beforeDelete.Serialize(), "undo restores deleted object ID and ordering");
                auto brokenSnapshot=beforeDelete;
                brokenSnapshot.objects.back().model="Assets/Models/Title/Roads/missing.obj";
                Check(!editorWorld.ReplaceLayout(brokenSnapshot, content, operationError) &&
                    editorWorld.Layout().Serialize()==beforeDelete.Serialize(), "failed history restoration preserves the whole live scene");
                Check(editorWorld.ReplaceLayout(afterDelete, content, operationError) &&
                    editorWorld.Layout().Serialize()==afterDelete.Serialize(), "redo restores the deletion snapshot");
                const auto editedPath = std::filesystem::absolute("generated/tests/layout-io/object-edits.json");
                editorWorld.Layout().Save(editedPath);
                Check(renderer.Render({ 0, 0, 0, 1 }, [&](ID3D12GraphicsCommandList* commands, float)
                {
                    editorWorld.Draw(commands, editorView, {});
                }) != Engine::RenderResult::Failed, "added and duplicated objects render after deletion");
                Check(renderer.WaitForIdle(), "object editing GPU completion");
                Check(editorWorld.Reload(content, editedPath, operationError) &&
                    editorWorld.Layout().objects.size() == count + 1 &&
                    editorWorld.Layout().objects.back().id == duplicateId, "object changes survive save and reload");
                App::TitleScene title(TestContentRoot());
                Check(title.Initialize(renderer), "title assets and sprite pipeline");
                Check(title.Draw(renderer) != Engine::RenderResult::Failed, "title rendering");
                Check(renderer.WaitForIdle(), "title GPU completion");
                App::TitleEnvironment environment;
                Check(environment.Initialize(renderer, TestContentRoot()), "ambient environment assets");
                for (int state = 0; state < 3; ++state)
                {
                    for (int frame = 0; frame < 30; ++frame) environment.Update(0.1, state != 1, true);
                    Check(renderer.Render({ 0, 0, 0, 1 }, [&](ID3D12GraphicsCommandList* commands, float)
                    {
                        environment.Draw(commands, renderer.GetWidth(), renderer.GetHeight());
                    }) != Engine::RenderResult::Failed, "ambient ON OFF resume rendering");
                    Check(renderer.WaitForIdle(), "ambient GPU completion");
                }
                App::TitleUi ui;
                Check(ui.Initialize(renderer, TestContentRoot()), "settings UI assets");
                App::TitleMenu animated(true);
                App::TitleMenuInput animationInput;
                animationInput.active = true;
                for (int frame = 0; frame < 14; ++frame)
                {
                    animationInput.keyboardButtons = frame >= 8 ? App::MenuConfirm : 0;
                    animated.Update(animationInput, 0.1);
                    Check(renderer.Render({ 0, 0, 0, 1 }, [&](ID3D12GraphicsCommandList* commands, float)
                    {
                        ui.Draw(commands, renderer.GetWidth(), renderer.GetHeight(), animated);
                    }) != Engine::RenderResult::Failed, "animated title rendering");
                    Check(renderer.WaitForIdle(), "animation GPU completion");
                }
                App::TitleMenu menu;
                App::TitleMenuInput input;
                input.active = true;
                menu.Update(input);
                input.keyboardButtons = App::MenuDown;
                menu.Update(input);
                input.keyboardButtons = App::MenuConfirm;
                menu.Update(input);
                for (int row = 0; row < 3; ++row)
                {
                    menu.CompleteSave(false);
                    Check(renderer.Render({ 0, 0, 0, 1 }, [&](ID3D12GraphicsCommandList* commands, float)
                    {
                        ui.Draw(commands, renderer.GetWidth(), renderer.GetHeight(), menu);
                    }) != Engine::RenderResult::Failed, "settings UI rendering");
                    Check(renderer.WaitForIdle(), "settings GPU completion");
                    input.keyboardButtons = 0;
                    menu.Update(input);
                    input.keyboardButtons = App::MenuDown;
                    menu.Update(input);
                }
            }
            CheckGpuMessages(renderer.GetDevice());
        }
        Engine::Log::Shutdown();
    }

    void ValidateOcclusionSamples(ID3D12Resource* readback)
    {
        const D3D12_RANGE range{0, sizeof(UINT64) * 4};
        void* mapped = nullptr;
        Hr(readback->Map(0, &range, &mapped));
        const auto* samples = static_cast<const UINT64*>(mapped);
        const bool visible = samples[0] > 0 && samples[1] == samples[0] && samples[2] == samples[0] && samples[3] == 0;
        std::cout << "Occlusion samples: " << samples[0] << ", " << samples[1] << ", " << samples[2] << ", " << samples[3] << '\n';
        const D3D12_RANGE written{0, 0};
        readback->Unmap(0, &written);
        Check(visible, "mirrored fronts visible and backface culled");
    }

    void ValidateMirroredMesh()
    {
        ComPtr<IDXGIFactory4> factory;
        ComPtr<IDXGIAdapter> adapter;
        ComPtr<ID3D12Device> device;
        ComPtr<ID3D12CommandQueue> queue;
        Hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
        Hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
        Hr(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
        D3D12_COMMAND_QUEUE_DESC queueDescription{};
        Hr(device->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(&queue)));

        Engine::MeshData data;
        data.vertices = {
            {{-0.75f, -0.75f, 0.5f}, {0, 0, -1}, {0, 1}},
            {{0, 0.75f, 0.5f}, {0, 0, -1}, {0.5f, 0}},
            {{0.75f, -0.75f, 0.5f}, {0, 0, -1}, {1, 1}}
        };
        data.indices = {0, 1, 2};
        Engine::MeshRenderer mesh;
        Check(mesh.Initialize(device.Get(), queue.Get(), data, "Content/Shaders/Mesh.hlsl"), "mesh initialization");

        ComPtr<ID3D12Resource> target;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap.CreationNodeMask = heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC description{};
        description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        description.Width = description.Height = 128;
        description.DepthOrArraySize = description.MipLevels = 1;
        description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        Hr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
            D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&target)));
        ComPtr<ID3D12DescriptorHeap> targets;
        D3D12_DESCRIPTOR_HEAP_DESC targetDescription{};
        targetDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        targetDescription.NumDescriptors = 1;
        Hr(device->CreateDescriptorHeap(&targetDescription, IID_PPV_ARGS(&targets)));
        const auto rtv = targets->GetCPUDescriptorHandleForHeapStart();
        device->CreateRenderTargetView(target.Get(), nullptr, rtv);
        Engine::DepthBuffer depth;
        Check(depth.Initialize(device.Get(), 128, 128), "test depth buffer");
        const auto dsv = depth.GetHandle();

        constexpr UINT CaseCount = 4;
        ComPtr<ID3D12QueryHeap> queries;
        D3D12_QUERY_HEAP_DESC queryDescription{};
        queryDescription.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION;
        queryDescription.Count = CaseCount;
        Hr(device->CreateQueryHeap(&queryDescription, IID_PPV_ARGS(&queries)));
        ComPtr<ID3D12Resource> readback;
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        description = {};
        description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        description.Width = sizeof(UINT64) * CaseCount;
        description.Height = description.DepthOrArraySize = description.MipLevels = 1;
        description.SampleDesc.Count = 1;
        description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        Hr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)));

        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> commands;
        ComPtr<ID3D12Fence> fence;
        Hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
        Hr(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commands)));
        Hr(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
        const D3D12_VIEWPORT viewport{0, 0, 128, 128, 0, 1};
        const D3D12_RECT scissor{0, 0, 128, 128};
        commands->RSSetViewports(1, &viewport);
        commands->RSSetScissorRects(1, &scissor);
        commands->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
        DirectX::XMFLOAT4X4 identity;
        DirectX::XMStoreFloat4x4(&identity, DirectX::XMMatrixIdentity());
        // 通常、X 鏡映、XY 鏡映、裏向き。最後のケースで裏面除去の維持も検証します。
        const std::array<DirectX::XMFLOAT3, CaseCount> scales{{{1, 1, 1}, {-1, 1, 1}, {-1, -1, 1}, {1, 1, -1}}};
        for (UINT i = 0; i < CaseCount; ++i)
        {
            const float clear[4]{};
            commands->ClearRenderTargetView(rtv, clear, 0, nullptr);
            commands->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0, nullptr);
            DirectX::XMFLOAT4X4 world;
            DirectX::XMStoreFloat4x4(&world, DirectX::XMMatrixScaling(scales[i].x, scales[i].y, scales[i].z) *
                DirectX::XMMatrixTranslation(0, 0, i == 3 ? 1.0f : 0.0f));
            commands->BeginQuery(queries.Get(), D3D12_QUERY_TYPE_OCCLUSION, i);
            mesh.Draw(commands.Get(), world, identity, {}, {0, 0, -1});
            commands->EndQuery(queries.Get(), D3D12_QUERY_TYPE_OCCLUSION, i);
        }
        commands->ResolveQueryData(queries.Get(), D3D12_QUERY_TYPE_OCCLUSION, 0, CaseCount, readback.Get(), 0);
        Hr(commands->Close());
        ID3D12CommandList* lists[] = {commands.Get()};
        queue->ExecuteCommandLists(1, lists);
        Check(Engine::SignalGpuFence(device.Get(), queue.Get(), fence.Get(), 1) &&
            Engine::WaitForGpuFence(device.Get(), fence.Get(), 1, nullptr), "mirror GPU completion");
        ValidateOcclusionSamples(readback.Get());
        CheckGpuMessages(device.Get());
    }
}

void ValidateEditorCamera()
{
#if defined(_DEBUG)
    Engine::DebugCamera camera;
    const std::array<float, 3> home{ -0.8f, 2.8f, -7.0f };
    Check(camera.SetResetPose(home, DirectX::XM_PIDIV2, 0.0f), "editor initial pose accepted");
    camera.Move(0, 0, 1, 1.0, false);
    Check(camera.GetPosition()[0] > home[0] && std::abs(camera.GetPosition()[2] - home[2]) < 0.001f,
        "free camera movement follows initial title orientation");
    camera.Rotate(200, 100);
    camera.Reset();
    Check(camera.GetPosition() == home, "editor reset restores title position");
    camera.Move(0, 0, 1, 1.0, false);
    Check(std::abs(camera.GetPosition()[2] - home[2]) < 0.001f, "reset restores orientation too");
    Check(!camera.SetResetPose(home, NAN, 0), "invalid reset pose rejected");
    camera.Reset();
    Check(camera.GetPosition() == home, "invalid reset does not overwrite saved pose");
#endif
}

void ValidateSceneLayout()
{
    const auto layout = SceneRuntime::SceneLayout::Load("Content/Assets/Scenes/TitleStreet.json");
    Check(layout.objects.size() == 123, "all existing street placements migrated");
    Check(layout.objects.front().id == "ground" && layout.objects.front().position[2] == 60.0f,
        "ground placement preserved");
    const std::string entry = R"({"id":"test","name":"Test","model":"Assets/Models/Title/Roads/ground.obj","position":[1,2,3],"rotation":[0,1,0],"scale":[4,4,4]})";
    const auto parse = [](const std::string& objects) {
        return SceneRuntime::SceneLayout::Parse("{\"version\":1,\"objects\":[" + objects + "]}");
    };
    Check(parse(entry).objects[0].rotation[1] == 1.0f, "full transform read from JSON");
    const auto reject = [](const std::string& json) {
        bool rejected = false;
        try { static_cast<void>(SceneRuntime::SceneLayout::Parse(json)); }
        catch (const std::exception&) { rejected = true; }
        Check(rejected, "invalid layout rejected");
    };
    reject("{invalid}");
    reject("{\"version\":2,\"objects\":[]}");
    reject("{\"version\":1,\"objects\":[" + entry + "," + entry + "]}");
    auto invalid = entry;
    invalid.replace(invalid.find("[4,4,4]"), 7, "[0,4,4]");
    reject("{\"version\":1,\"objects\":[" + invalid + "]}");
    invalid = entry;
    invalid.replace(invalid.find("Roads/ground.obj"), 15, "../ground.obj");
    reject("{\"version\":1,\"objects\":[" + invalid + "]}");
}

void ValidateSceneFiles()
{
    auto layout = SceneRuntime::SceneLayout::Load("Content/Assets/Scenes/TitleStreet.json");
    layout.objects.front().name = "テスト\"看板\n二行目";
    layout.objects.front().rotation = { 0.1234567f, -0.9876543f, 1.234567f };
    const auto directory = std::filesystem::absolute("generated/tests/layout-io");
    std::filesystem::create_directories(directory);
    const auto path = directory / "roundtrip.json";
    layout.Save(path);
    auto restored = SceneRuntime::SceneLayout::Load(path);
    Check(restored.objects.size() == layout.objects.size() &&
        restored.objects.front().name == layout.objects.front().name &&
        restored.objects.front().rotation == layout.objects.front().rotation &&
        restored.objects.back().model == layout.objects.back().model, "layout UTF8 and transforms round-trip");
    layout.objects.front().position[0] = 3.25f;
    layout.Save(path);
    Check(SceneRuntime::SceneLayout::Load(path).objects.front().position[0] == 3.25f, "save replaces existing layout");
    HANDLE locked = CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(locked != INVALID_HANDLE_VALUE, "lock layout for save failure test");
    bool failed = false;
    layout.objects.front().position[0] = 9.0f;
    try { layout.Save(path); } catch (const std::exception&) { failed = true; }
    CloseHandle(locked);
    Check(failed && SceneRuntime::SceneLayout::Load(path).objects.front().position[0] == 3.25f,
        "failed replacement preserves original file");
    for (const auto& file : std::filesystem::directory_iterator(directory))
        Check(file.path().extension() != ".tmp", "failed save removes temporary file");
    layout.objects.front().scale[0] = 0;
    failed = false;
    try { layout.Save(path); } catch (const std::exception&) { failed = true; }
    Check(failed && SceneRuntime::SceneLayout::Load(path).objects.front().position[0] == 3.25f,
        "invalid save leaves existing file intact");
    layout.objects.front().scale[0] = 0.000001f;
    Check(SceneRuntime::SceneLayout::Parse(layout.Serialize()).objects.front().scale[0] == 0.000001f,
        "minimum editable scale can be saved and loaded");
}

void ValidateTransformMatrix()
{
    SceneRuntime::ScenePlacement placement;
    placement.position={8,-2,12};
    for (const auto& scale : {std::array<float,3>{2,3,4}, std::array<float,3>{-2,3,4}, std::array<float,3>{-2,-3,4}})
    for (float y : {0.3f, 2.1f, DirectX::XM_PIDIV2, -DirectX::XM_PIDIV2})
    {
        placement.scale=scale;
        placement.rotation={0.7f,y,-0.4f};
        const auto matrix=Editor::TransformMatrix::Compose(placement);
        auto result=placement;
        Check(Editor::TransformMatrix::Read(matrix,placement,result), "gizmo matrix round trip including mirrored and gimbal poses");
        for (int i=0;i<3;++i)
            Check(std::abs(result.scale[i]-scale[i])<0.001f && std::abs(result.rotation[i]-placement.rotation[i])<0.001f,
                "gizmo conversion preserves mirror signs and continuous Euler angles");
    }
    placement.rotation={0,0,0};
    placement.scale={1,1,1};
    auto invalid=Editor::TransformMatrix::Compose(placement);
    invalid._12=0.5f;
    auto output=placement;
    Check(!Editor::TransformMatrix::Read(invalid,placement,output) && output.rotation==placement.rotation,
        "sheared gizmo matrix is rejected without changing placement");
    invalid=Editor::TransformMatrix::Compose(placement);
    invalid._11=0;
    Check(!Editor::TransformMatrix::Read(invalid,placement,output), "zero scale is rejected");
    invalid._11=NAN;
    Check(!Editor::TransformMatrix::Read(invalid,placement,output), "nonfinite transform is rejected");
}

void ValidateEditHistory()
{
    Editor::EditHistory history;
    history.Reset({"initial", "a"});
    history.Observe({"drag1", "a"}, true);
    history.Observe({"drag2", "a"}, true);
    Check(!history.CanUndo(), "ongoing drag is not a history entry");
    history.Observe({"drag2", "a"}, false);
    Check(history.CanUndo() && history.Target(false).json=="initial", "drag becomes one undo entry");
    history.Saved("drag2");
    Check(!history.Dirty("drag2") && history.Dirty("initial"), "saved content determines dirty state");
    history.Applied(false);
    Check(history.CanRedo() && history.Target(true).selection=="a", "redo preserves selection");
    history.Observe({"branch", "b"}, false);
    Check(!history.CanRedo() && history.Target(false).json=="initial", "new edit clears redo branch");
    history.Reset({"reloaded", ""});
    Check(!history.CanUndo() && !history.CanRedo() && !history.Dirty("reloaded"), "reload resets history and saved state");
    for (int i=0;i<150;++i) history.Observe({std::to_string(i), ""}, false);
    int undoCount=0;
    while (history.CanUndo()) { history.Applied(false); ++undoCount; }
    Check(undoCount==100, "history is bounded to 100 edits");
}
int main()
{
    try
    {
        ValidateTransformMatrix();
        ValidateEditHistory();
        ValidateEditorCamera();
        ValidateSceneLayout();
        ValidateSceneFiles();
        ValidateDiagnostics();
        ValidateTitleMenu();
        ValidateTitleAnimation();
        ValidateSettings();
        ValidateAmbientMotion();
        ValidateTitleAudio();
        ValidateTitle();
        ValidateMirroredMesh();
        std::cout << "PASS: diagnostics location/overrides, title menu/settings/rendering, mirrored mesh visibility and backface culling\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
