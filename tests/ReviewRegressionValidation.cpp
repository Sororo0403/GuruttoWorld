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
        App::TitleAmbientMotion motion;
        App::TitleAmbientMotion exitCamera;
        const auto home = exitCamera.CameraPosition();
        Engine::Camera framing;
        framing.SetPosition(home);
        framing.SetRotation(0.03f, 0.13f);
        framing.SetPerspective(DirectX::XM_PIDIV4, 16.0f / 9.0f, 0.1f, 220.0f);
        for (int frame = 0; frame < 7; ++frame) exitCamera.Update(0.1, false, true, false, true);
        Check(std::abs(exitCamera.CameraPosition()[2] - 7.0f) < 0.001f &&
            std::abs(exitCamera.CameraRotation()[0] - 0.15f) < 0.001f,
            "exit hover frames gate in the building row within 0.7 seconds");
        framing.SetPosition(exitCamera.CameraPosition());
        framing.SetRotation(exitCamera.CameraRotation()[0], exitCamera.CameraRotation()[1]);
        const auto gateCenter = DirectX::XMVector3TransformCoord(
            DirectX::XMVectorSet(4.8f, 3.0f, 19.0f, 1.0f),
            framing.GetViewMatrix() * framing.GetProjectionMatrix());
        Check(DirectX::XMVectorGetX(gateCenter) > 0.0f && DirectX::XMVectorGetX(gateCenter) < 0.8f,
            "exit hover reveals gate on right of menu");
        const auto exitPose = exitCamera.CameraPosition();
        exitCamera.Update(0.1, false, false, true, false);
        Check(exitCamera.CameraPosition() == exitPose, "inactive exit camera pauses");
        exitCamera.Update(0.1, false, true, true, false);
        Check(std::abs(exitCamera.CameraPosition()[2] - exitPose[2]) < 0.5f,
            "switching exit to settings starts smoothly");
        for (int frame = 0; frame < 20; ++frame) exitCamera.Update(0.1, false, true, true, false);
        Check(exitCamera.CameraRotation()[0] < -0.8f, "exit to settings turns toward control facility");
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
                position[1] >= 1.759f && position[1] <= 1.841f && position[2] == -7.0f, "camera stays within composition bounds");
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
                App::TitleScene title(std::filesystem::absolute("App"));
                Check(title.Initialize(renderer), "title assets and sprite pipeline");
                Check(title.Draw(renderer) != Engine::RenderResult::Failed, "title rendering");
                Check(renderer.WaitForIdle(), "title GPU completion");
                App::TitleEnvironment environment;
                Check(environment.Initialize(renderer, std::filesystem::absolute("App")), "ambient environment assets");
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
                Check(ui.Initialize(renderer, std::filesystem::absolute("App")), "settings UI assets");
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
        Check(mesh.Initialize(device.Get(), queue.Get(), data, "App/Shaders/Mesh.hlsl"), "mesh initialization");

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

int main()
{
    try
    {
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
