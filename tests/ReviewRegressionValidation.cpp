#include <SceneRuntime/SceneView.h>
#include "../Editor/src/GizmoTransform.h"
#include "../Editor/src/EditHistory.h"
#include "../Editor/src/EditState.h"
#include "../Editor/src/PlayState.h"
#include "../Editor/src/GameSession.h"
#include "../Editor/src/PlaySnapshot.h"
#include "../Editor/src/SceneViewport.h"
#include "../Editor/src/ModelDrop.h"
#include "../Editor/src/ConsoleFilter.h"
#include "../Editor/src/SceneDocument.h"
#include "../Editor/src/HierarchyRows.h"
#include "../Editor/src/ProjectCatalog.h"
#include "../Editor/src/AssetInfo.h"
#include "../Editor/src/AssetChanges.h"
#include "../Editor/src/FocusSelection.h"
#if defined(_DEBUG)
#include <Engine/DevTools/DebugCamera.h>
#include "../Editor/src/ScenePanel.h"
#include "../Editor/src/ObjectPanel.h"
#include "../Editor/src/AssetPreview.h"
#endif
#include <SceneRuntime/SceneLayout.h>
#include <SceneRuntime/SceneTransforms.h>
#include <SceneRuntime/SceneWorld.h>
#include "AuthoredViewFixture.h"
#include "EnvironmentValidation.h"
#include "UiValidation.h"
#include "EditorFontValidation.h"
#include <Engine/Core/DiagnosticPaths.h>
#include <Engine/Core/Log.h>
#include <Engine/Core/CrashHandler.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <Engine/Graphics/DirectX12/GpuSynchronization.h>
#include <Engine/Graphics/Renderers/MeshRenderer.h>
#include <Engine/Graphics/Resources/DepthBuffer.h>
#include <Engine/Graphics/Resources/RenderTexture.h>
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
    DirectX::XMFLOAT4X4 ComposeTransform(const SceneRuntime::ScenePlacement& placement)
    {
        DirectX::XMFLOAT4X4 matrix;
        Check(SceneRuntime::SceneTransforms::Compose(placement,matrix), "compose validated transform fixture");
        return matrix;
    }
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
        Check(GameSettings::UserPath() == Engine::GetDiagnosticsRoot() / "settings.json", "user settings location");
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
        { std::ofstream legacy(folder/"legacy.txt"); legacy << "WP1_SETTINGS 1 4 0"; }
        const auto legacy=GameSettings::Load(folder/"legacy.json");
        Check(legacy.volume==4 && !legacy.backgroundMotion,"legacy settings migrate through JSON path");
        Check(legacy.Save(folder/"legacy.json"),"migrated settings save JSON");
        for(const auto* invalid:{R"({"version":1,"volume":3.5,"backgroundMotion":true})",R"({"version":1,"volume":3,"backgroundMotion":1})",R"({"version":true,"volume":3,"backgroundMotion":false})"}) {
            {std::ofstream stream(path); stream<<invalid;}
            Check(GameSettings::Load(path).volume==10,"JSON settings reject wrong types");
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

    void ValidatePressAnyTitle()
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
            const auto sound = audio.Load(std::filesystem::path("Content/Assets/Audio/Title") / file);
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

    void ValidateTransformApi(SceneRuntime::SceneWorld& world, const std::filesystem::path& root)
    {
        const auto initial=world.Layout();
        std::string error;
        auto child=initial.objects[1]; child.position={1,2,3}; child.rotation={0,0,0}; child.scale={1,1,1};
        DirectX::XMFLOAT4X4 parent{},actual{},expected{};
        Check(world.WorldMatrix(child.parentId,parent), "explicit transform API parent matrix");
        const auto local=ComposeTransform(child);
        DirectX::XMStoreFloat4x4(&expected,DirectX::XMLoadFloat4x4(&local)*DirectX::XMLoadFloat4x4(&parent));
        Check(world.SetLocalTransform(child.id,child.position,child.rotation,child.scale) && world.WorldMatrix(child.id,actual) &&
            SceneRuntime::SceneTransforms::Matches(actual,expected), "local setter uses parent-relative coordinates");
        Check(world.SetWorldTransform(child.id,{50,60,70},{0,0,0},{1,1,1}) && world.WorldMatrix(child.id,actual) &&
            std::abs(actual._41-50)<0.001f && std::abs(actual._42-60)<0.001f && std::abs(actual._43-70)<0.001f,
            "world setter converts through parent into local storage");
        const auto snapshot=world.Layout().Serialize();
        expected=actual; expected._12+=0.5f;
        Check(!world.SetWorldTransform(child.id,expected) && world.Layout().Serialize()==snapshot &&
            world.WorldMatrix(child.id,parent) && SceneRuntime::SceneTransforms::Matches(parent,actual),
            "world matrix setter rejects unrepresentable shear without changing draw or data");
        SceneRuntime::ScenePlacement output=child;
        Check(!world.LocalTransformFromWorld("missing",actual,output) && output.position==child.position,
            "matrix conversion preserves output for missing objects");
        Check(!world.SetLocalTransform("missing",child.position,child.rotation,child.scale) &&
            !world.SetWorldTransform("missing",actual), "explicit setters reject missing objects");
        Check(world.ReplaceLayout(initial,root,error), "explicit transform API fixture restored");
    }

    void ValidateHierarchyMutations(SceneRuntime::SceneWorld& world, const std::filesystem::path& root)
    {
        const auto initial=world.Layout();
        auto parent=initial.objects.back(); parent.id="parent"; parent.parentId.clear();
        parent.position={10,20,30}; parent.rotation={0,0.4f,0}; parent.scale={-2,2,2};
        auto child=parent; child.id="child"; child.parentId=parent.id;
        child.position={1,2,3}; child.rotation={0,0,0.37f}; child.scale={1,1,1};
        auto grandchild=child; grandchild.id="grandchild"; grandchild.parentId=child.id;
        auto other=parent; other.id="other"; other.position={-5,3,4}; other.rotation={0.2f,0.1f,0.3f}; other.scale={3,3,3};
        SceneRuntime::SceneLayout layout; layout.objects={grandchild,child,parent,other};
        std::string error,created;
        Check(world.ReplaceLayout(layout,root,error), "rotated mirrored hierarchy mutation fixture");
        DirectX::XMFLOAT4X4 childBefore{},grandBefore{},actual{};
        Check(world.WorldMatrix("child",childBefore) && world.WorldMatrix("grandchild",grandBefore), "capture hierarchy world poses");
        Check(world.SetParent("child","other",error) && world.WorldMatrix("child",actual) &&
            SceneRuntime::SceneTransforms::Matches(childBefore,actual) && world.WorldMatrix("grandchild",actual) &&
            SceneRuntime::SceneTransforms::Matches(grandBefore,actual), "reparent between rotated mirrored parents preserves subtree pose");
        const auto unchanged=world.Layout().Serialize();
        Check(world.SetParent("child","other",error) && error.empty() && world.Layout().Serialize()==unchanged,
            "same parent is an exact no-op without Euler or scale drift");
        Check(world.SetParent("child",{},error) && world.WorldMatrix("grandchild",actual) &&
            SceneRuntime::SceneTransforms::Matches(grandBefore,actual), "root detach preserves reflected descendant pose");
        Check(world.ReplaceLayout(layout,root,error), "restore hierarchy before duplicate");
        Check(world.SetLocalTransform("parent",parent.position,parent.rotation,{-2,3,4}), "nonuniform mirrored duplicate fixture");
        const auto source=world.Layout().objects[1];
        Check(world.WorldMatrix("child",childBefore) && world.DuplicateObject("child",{5,-2,3},created,error), "duplicate uses world offset under shear");
        const auto duplicate=world.Layout().objects.back();
        Check(duplicate.parentId==source.parentId && duplicate.rotation==source.rotation && duplicate.scale==source.scale &&
            world.WorldMatrix(created,actual) && std::abs(actual._41-childBefore._41-5)<0.001f &&
            std::abs(actual._42-childBefore._42+2)<0.001f && std::abs(actual._43-childBefore._43-3)<0.001f,
            "duplicate preserves local rotation scale and parent while shifting only world position");
        auto plain=child; plain.id="plain"; plain.rotation={0,0,0};
        layout.objects={plain,child,grandchild,parent,other}; layout.objects[3].rotation={0,0,0}; layout.objects[3].scale={2,3,4};
        Check(world.ReplaceLayout(layout,root,error), "partial deletion validation fixture");
        const auto before=world.Layout().Serialize();
        Check(world.WorldMatrix("plain",childBefore) && !world.RemoveObjects({"parent"},error) &&
            error.find("child")!=std::string::npos && world.Layout().Serialize()==before &&
            world.WorldMatrix("plain",actual) && SceneRuntime::SceneTransforms::Matches(childBefore,actual),
            "later unrepresentable child cancels whole deletion including earlier representable child");
        layout.objects[3].scale={-2,2,2};
        Check(world.ReplaceLayout(layout,root,error) && world.WorldMatrix("grandchild",grandBefore), "representable deletion fixture");
        const auto original=world.Layout().Serialize();
        Editor::EditHistory history; history.Reset({original,"grandchild",{"child","grandchild"}});
        Check(world.RemoveObjects({"parent"},error) && error.empty() && world.WorldMatrix("grandchild",actual) &&
            SceneRuntime::SceneTransforms::Matches(grandBefore,actual) && world.Layout().objects[0].parentId.empty() &&
            world.Layout().objects[1].parentId.empty() && world.Layout().objects[2].parentId=="child",
            "delete only parent, promoting direct children while retaining grandchild hierarchy and pose");
        history.Observe({world.Layout().Serialize(),"grandchild",{"child","grandchild"}},{});
        Check(world.ReplaceLayout(SceneRuntime::SceneLayout::Parse(history.Target(false).json),root,error) &&
            world.Layout().Serialize()==original, "Undo restores parent deletion with exact local transforms");
        history.Applied(false);
        Check(world.ReplaceLayout(SceneRuntime::SceneLayout::Parse(history.Target(true).json),root,error) &&
            world.WorldMatrix("grandchild",actual) && SceneRuntime::SceneTransforms::Matches(grandBefore,actual), "Redo preserves promoted subtree pose");
        Check(world.ReplaceLayout(initial,root,error), "hierarchy mutation fixture restored");
    }

    void ValidateGizmoTransforms(SceneRuntime::SceneWorld& world, const std::filesystem::path& root)
    {
        const auto initial=world.Layout();
        auto parent=initial.objects.back(); parent.id="parent"; parent.parentId.clear();
        parent.position={10,20,30}; parent.rotation={0.2f,0.4f,0.1f}; parent.scale={-2,3,4};
        auto child=parent; child.id="child"; child.parentId=parent.id;
        child.position={1,2,3}; child.rotation={0.3f,-0.2f,0.37f}; child.scale={-1,2,3};
        auto grandchild=child; grandchild.id="grandchild"; grandchild.parentId=child.id;
        SceneRuntime::SceneLayout layout; layout.objects={grandchild,child,parent};
        std::string error;
        Check(world.ReplaceLayout(layout,root,error), "gizmo nonuniform reflected parent fixture");
        DirectX::XMFLOAT4X4 handle{},draw{},orientation{},expected{};
        Check(Editor::GizmoTransform::Build(world,child,false,handle) && world.WorldMatrix(child.id,draw) &&
            world.WorldRotation(child.id,orientation), "gizmo frame separates rotation from inherited shear");
        auto rotationChild=child,rotationParent=parent;
        rotationChild.position={}; rotationChild.scale={1,1,1}; rotationParent.position={}; rotationParent.scale={1,1,1};
        const auto childRotation=ComposeTransform(rotationChild);
        const auto parentRotation=ComposeTransform(rotationParent);
        DirectX::XMStoreFloat4x4(&expected,DirectX::XMLoadFloat4x4(&childRotation)*DirectX::XMLoadFloat4x4(&parentRotation));
        Check(SceneRuntime::SceneTransforms::Matches(expected,orientation) && handle._41==draw._41 && handle._42==draw._42 &&
            handle._43==draw._43, "handle uses ancestor rotations and actual world pivot");
        const float drawDot=draw._11*draw._21+draw._12*draw._22+draw._13*draw._23;
        const float handleDot=handle._11*handle._21+handle._12*handle._22+handle._13*handle._23;
        Check(std::abs(drawDot)>0.01f && std::abs(handleDot)<0.001f, "sheared geometry retains orthogonal handle axes");
        DirectX::XMStoreFloat4x4(&expected,DirectX::XMLoadFloat4x4(&orientation)*DirectX::XMMatrixRotationY(0.25f));
        expected._41=draw._41; expected._42=draw._42; expected._43=draw._43;
        auto result=child;
        Check(Editor::GizmoTransform::ReadRotation(world,child,expected,result) && result.position==child.position &&
            result.scale==child.scale && world.SetLocalTransform(child.id,result.position,result.rotation,result.scale) &&
            world.WorldRotation(child.id,orientation), "world rotation edits only local rotation under nonuniform parent");
        expected._41=0; expected._42=0; expected._43=0;
        Check(SceneRuntime::SceneTransforms::Matches(expected,orientation), "world rotation follows selected world axis");
        auto localDelta=rotationChild; localDelta.rotation={0,0,0.2f};
        const auto delta=ComposeTransform(localDelta);
        DirectX::XMStoreFloat4x4(&expected,DirectX::XMLoadFloat4x4(&delta)*DirectX::XMLoadFloat4x4(&childRotation)*
            DirectX::XMLoadFloat4x4(&parentRotation));
        Check(Editor::GizmoTransform::ReadRotation(world,child,expected,result) &&
            world.SetLocalTransform(child.id,result.position,result.rotation,result.scale) && world.WorldRotation(child.id,orientation) &&
            SceneRuntime::SceneTransforms::Matches(expected,orientation), "local rotation follows the object axis through parent rotation");
        child=world.Layout().objects[1];
        Check(Editor::GizmoTransform::Build(world,child,true,handle), "local scale handle builds with signed local scale");
        DirectX::XMStoreFloat4x4(&expected,DirectX::XMMatrixScaling(1.5f,0.5f,2)*DirectX::XMLoadFloat4x4(&handle));
        Check(Editor::GizmoTransform::ReadScale(child,expected,result) && result.position==child.position &&
            result.rotation==child.rotation && std::abs(result.scale[0]-child.scale[0]*1.5f)<0.001f &&
            std::abs(result.scale[1]-child.scale[1]*0.5f)<0.001f && std::abs(result.scale[2]-child.scale[2]*2)<0.001f,
            "scale changes signed local scale without changing local rotation or position");
        const auto preserved=result;
        expected._11=0; expected._12=0; expected._13=0;
        Check(!Editor::GizmoTransform::ReadScale(child,expected,result) && result.scale==preserved.scale,
            "zero scale handle rejects without changing output");
        expected=handle; expected._14=0.1f;
        Check(!Editor::GizmoTransform::ReadRotation(world,child,expected,result) && result.scale==preserved.scale,
            "invalid affine rotation rejects without changing output");
        Check(world.WorldMatrix("grandchild",draw) && world.TranslateObjectsWorld({"parent","child","grandchild"},{2,0,0}) &&
            world.WorldMatrix("grandchild",handle) && std::abs(handle._41-draw._41-2)<0.001f,
            "gizmo group move still avoids double transform for selected descendants");
        Check(world.ReplaceLayout(initial,root,error), "gizmo transform fixture restored");
    }

    void ValidateEditTransactions(SceneRuntime::SceneWorld& world, const std::filesystem::path& root)
    {
        const auto initial=world.Layout();
        const auto child=initial.objects[1];
        Editor::EditState state; state.Select(child.id); state.Select("grandchild",true);
        const auto selection=state.SelectedIds();
        Editor::EditHistory history;
        const auto original=initial.Serialize();
        history.Reset({original,state.SelectedId(),selection});
        Check(state.SetLocalTransform(world,child.id,child.position,child.rotation,child.scale) &&
            state.Rename(world,child.id,child.name) && state.TranslateSelectionWorld(world,{0,0,0}) && !state.HasChanges(),
            "no-op transform rename and move do not mark scene dirty");
        Check(!state.SetLocalTransform(world,child.id,child.position,child.rotation,{0,1,1}) &&
            !state.HasChanges() && state.SelectedIds()==selection && world.Layout().Serialize()==original,
            "failed transform preserves scene selection and clean state");
        for (int step=0;step<3;++step)
        {
            state.BeginFrame(); state.SetInteraction("gizmo/child/move");
            Check(state.TranslateSelectionWorld(world,{1,0,0}), "transaction drag applies validated group movement");
            history.Observe({world.Layout().Serialize(),state.SelectedId(),state.SelectedIds()},state.Interaction());
        }
        const auto moved=world.Layout().Serialize();
        Check(state.HasChanges() && !history.CanUndo() && state.SelectedIds()==selection,
            "continuous drag stays pending and preserves multiple selection");
        Check(!state.TranslateSelectionWorld(world,{NAN,0,0}) && state.HasChanges() && world.Layout().Serialize()==moved &&
            state.SelectedIds()==selection, "failed intermediate drag keeps last valid result and prior dirty state");
        state.BeginFrame();
        history.Observe({moved,state.SelectedId(),state.SelectedIds()},state.Interaction());
        std::string error,created;
        Check(history.CanUndo() && history.Target(false).json==original &&
            world.ReplaceLayout(SceneRuntime::SceneLayout::Parse(history.Target(false).json),root,error),
            "one drag commits exactly one undo entry");
        history.Applied(false); state.SetChanged(history.Dirty(world.Layout().Serialize()));
        Check(!history.CanUndo() && history.CanRedo() && !state.HasChanges(), "undo returns directly to saved clean baseline");
        Check(!state.SetLocalTransform(world,"missing",child.position,child.rotation,child.scale), "invalid edit after undo fails");
        history.Observe({world.Layout().Serialize(),state.SelectedId(),state.SelectedIds()},{});
        Check(history.CanRedo() && !state.HasChanges(), "failed edit preserves redo branch and dirty state");
        Check(world.DuplicateObject(child.id,{0,0,0},created,error), "allocation baseline duplicate");
        const auto next=std::stoull(created.substr(7))+1;
        Check(world.RemoveObjects({created},error), "allocation baseline duplicate removed");
        auto invalid=child; invalid.id.clear(); invalid.SetModel("Assets/Models/Title/missing-transaction-model.obj");
        Check(!world.AddObject(invalid,root,created,error) && created.empty(), "failed model creation cancels ID allocation");
        Check(world.DuplicateObject(child.id,{0,0,0},created,error) && created=="object-"+std::to_string(next),
            "next successful creation reuses ID reserved by failed operation");
        Check(world.ReplaceLayout(initial,root,error), "transaction fixture restored");
    }

    void ValidateTransformWorkflow(Engine::DirectX12Renderer& renderer, SceneRuntime::SceneWorld& world,
        const std::filesystem::path& root)
    {
        const auto initial=world.Layout();
        const auto directory=std::filesystem::absolute("generated/tests/transform-workflow");
        std::filesystem::create_directories(directory);
        const auto path=directory/"New.json", copy=directory/"Copy.json";
        std::filesystem::remove(path); std::filesystem::remove(copy);
        Editor::SceneDocument document(path);
        Editor::EditState state;
        Editor::EditHistory history;
        std::string error,created;
        Check(renderer.WaitForIdle() && document.Request(path,true,false) && document.Apply(world,root,error) &&
            world.Layout().objects.empty(), "workflow creates a new local scene");
        history.Reset({world.Layout().Serialize(),{}, {}});
        auto parent=initial.objects.back(); parent.id="parent"; parent.parentId.clear();
        parent.position={10,0,0}; parent.rotation={0,0.3f,0}; parent.scale={2,2,2};
        auto child=parent; child.id="child"; child.position={3,1,0}; child.rotation={0.2f,0.1f,-0.4f}; child.scale={1,1,1};
        auto grandchild=child; grandchild.id="grandchild"; grandchild.parentId=child.id; grandchild.position={0,1,0};
        const auto observe=[&]() { history.Observe({world.Layout().Serialize(),state.SelectedId(),state.SelectedIds()},state.Interaction()); };
        for (const auto& placement : {parent,child,grandchild})
        {
            state.BeginFrame();
            Check(world.AddObject(placement,root,created,error), "workflow adds parent child and grandchild");
            state.ObjectChanged(created); observe();
        }
        DirectX::XMFLOAT4X4 before{},after{};
        Check(world.WorldMatrix("grandchild",before), "workflow captures descendant world pose");
        state.BeginFrame();
        Check(state.SetParent(world,"child","parent",error) && world.WorldMatrix("grandchild",after) &&
            SceneRuntime::SceneTransforms::Matches(before,after), "workflow parents child while preserving descendant pose");
        observe(); state.BeginFrame();
        state.Select("child"); state.Select("grandchild",true); observe();
        const auto parented=world.Layout().Serialize();
        for (int step=0;step<4;++step)
        {
            state.BeginFrame(); state.SetInteraction("gizmo/grandchild/move");
            Check(state.TranslateSelectionWorld(world,{0.5f,0,0}), "workflow moves parented selection"); observe();
        }
        state.BeginFrame(); observe();
        const auto moved=world.Layout().Serialize();
        const auto apply=[&](bool redo)
        {
            const auto target=history.Target(redo);
            Check(world.ReplaceLayout(SceneRuntime::SceneLayout::Parse(target.json),root,error), "workflow history restores whole scene");
            history.Applied(redo); state.RestoreSelection(target.selections,target.selection);
            state.SetChanged(document.UnsavedNew() || history.Dirty(target.json));
        };
        apply(false);
        Check(world.Layout().Serialize()==parented && state.SelectedIds()==std::vector<std::string>{"child","grandchild"},
            "workflow Undo reverses one full drag with selection");
        apply(true);
        Check(world.Layout().Serialize()==moved, "workflow Redo restores full drag");
        document.Save(world.Layout()); history.Saved(moved); state.MarkSaved();
        Check(!document.UnsavedNew() && !state.HasChanges() && !history.Dirty(moved), "workflow saves clean baseline");
        state.BeginFrame();
        Check(world.DuplicateObject("child",{4,0,0},created,error), "workflow duplicates parented child");
        state.ObjectChanged(created); observe();
        const auto duplicated=world.Layout().Serialize();
        state.Select("parent"); observe();
        Check(world.WorldMatrix("grandchild",before) && world.RemoveObjects({"parent"},error), "workflow deletes parent");
        state.ObjectChanged({}); observe();
        Check(world.WorldMatrix("grandchild",after) && SceneRuntime::SceneTransforms::Matches(before,after),
            "workflow deletion preserves descendant world pose");
        apply(false);
        Check(world.Layout().Serialize()==duplicated && state.SelectedId()=="parent", "workflow Undo restores deleted parent and selection");
        apply(false);
        Check(world.Layout().Serialize()==moved && !state.HasChanges(), "workflow Undo duplicate returns to saved clean scene");
        apply(true); apply(true); apply(false); apply(false);
        Check(world.Layout().Serialize()==moved, "workflow Redo and Undo reproduce hierarchy mutations");
        document.SaveAs(world.Layout(),copy,false); history.Saved(moved); state.MarkSaved();
        Check(document.Path()==copy && SceneRuntime::SceneLayout::Load(path).Serialize()==moved,
            "workflow SaveAs changes document while retaining original saved scene");
        Engine::Camera camera; AuthoredViewFixture::SetHome(camera); AuthoredViewFixture::SetProjection(camera,1);
        Check(renderer.Render({0,0,0,1},[&](ID3D12GraphicsCommandList* commands,float) { world.Draw(commands,camera,{}); })!=
            Engine::RenderResult::Failed && renderer.WaitForIdle(), "workflow final hierarchy renders before reload");
        Check(document.Request(copy,false,false) && document.Apply(world,root,error) && world.Layout().Serialize()==moved &&
            world.Layout().objects[1].parentId=="parent" && world.Layout().objects[2].parentId=="child",
            "workflow reopen preserves saved hierarchy and local transforms");
        Check(world.ReplaceLayout(initial,root,error), "workflow fixture restored");
    }

    void ValidateMultipleScaling(SceneRuntime::SceneWorld& world, const std::filesystem::path& root)
    {
        const auto initial=world.Layout();
        auto parent=initial.objects.back(); parent.id="parent"; parent.parentId.clear();
        parent.position={10,2,3}; parent.rotation={0,0.4f,0}; parent.scale={-2,3,4};
        auto child=parent; child.id="child"; child.parentId=parent.id;
        child.position={1,2,3}; child.rotation={0,0,0.3f}; child.scale={1,1,1};
        auto grand=child; grand.id="grand"; grand.parentId=child.id;
        auto other=child; other.id="other"; other.parentId.clear(); other.position={20,3,4}; other.rotation=parent.rotation;
        SceneRuntime::SceneLayout layout; layout.objects={grand,child,parent,other};
        std::string error;
        Check(world.ReplaceLayout(layout,root,error), "multiple scaling fixture");
        Editor::EditState state; state.Select("parent"); state.Select("other",true); state.Select("child",true);
        const auto before=world.Layout().Serialize(); const auto selected=state.SelectedIds();
        std::vector<DirectX::XMFLOAT4X4> poses;
        for (const auto& placement : layout.objects)
        {
            DirectX::XMFLOAT4X4 pose;
            Check(world.WorldMatrix(placement.id,pose), "capture group scale world pose"); poses.push_back(pose);
        }
        const std::array<float,3> pivot{poses[1]._41,poses[1]._42,poses[1]._43};
        DirectX::XMFLOAT4X4 axes,identity,actual,expected;
        Check(world.WorldRotation("child",axes), "active child scaling axes");
        DirectX::XMStoreFloat4x4(&identity,DirectX::XMMatrixIdentity());
        const auto delta=DirectX::XMMatrixTranslation(-pivot[0],-pivot[1],-pivot[2])*
            DirectX::XMMatrixScaling(2,2,2)*DirectX::XMMatrixTranslation(pivot[0],pivot[1],pivot[2]);
        Editor::EditHistory history; history.Reset({before,state.SelectedId(),selected});
        state.SetInteraction("gizmo/child/2");
        Check(state.ScaleSelectionWorld(world,pivot,axes,{2,2,2}) && state.HasChanges() && state.SelectedIds()==selected,
            "group scaling succeeds around child pivot under mirrored nonuniform parent");
        for (size_t index=0;index<layout.objects.size();++index)
        {
            DirectX::XMStoreFloat4x4(&expected,DirectX::XMLoadFloat4x4(&poses[index])*delta);
            Check(world.WorldMatrix(layout.objects[index].id,actual) && SceneRuntime::SceneTransforms::Matches(expected,actual),
                "group scaling updates shape and spacing once including unselected descendants");
        }
        Check(world.Layout().objects[2].scale[0]<0 && world.Layout().objects[1].scale==child.scale &&
            world.Layout().objects[1].position==child.position && world.Layout().objects[0].scale==grand.scale,
            "scaling preserves mirror sign and exact descendant local transforms");
        const auto after=world.Layout().Serialize(); history.Observe({after,state.SelectedId(),selected},state.Interaction());
        state.BeginFrame(); history.Observe({after,state.SelectedId(),selected},{});
        const auto undo=history.Target(false);
        Check(world.ReplaceLayout(SceneRuntime::SceneLayout::Parse(undo.json),root,error), "one Undo restores scale drag");
        history.Applied(false); state.RestoreSelection(undo.selections,undo.selection); state.SetChanged(history.Dirty(undo.json));
        Check(world.Layout().Serialize()==before && !state.HasChanges() && state.SelectedIds()==selected, "scale Undo restores scene selection and clean state");
        Check(state.ScaleSelectionWorld(world,pivot,axes,{1,1,1}) && world.Layout().Serialize()==before && !state.HasChanges(),
            "unit scale factors are exact no-op despite rotated axes");
        Check(!world.ScaleObjectsWorld({},pivot,axes,{2,2,2}) && !world.ScaleObjectsWorld({"parent","missing"},pivot,axes,{2,2,2}) &&
            !world.ScaleObjectsWorld({"parent","parent"},pivot,axes,{2,2,2}) && !world.ScaleObjectsWorld(selected,{NAN,0,0},axes,{2,2,2}),
            "invalid scale selection and pivot rejected");
        Check(!world.ScaleObjectsWorld(selected,pivot,axes,{0,1,1}) && !world.ScaleObjectsWorld(selected,pivot,axes,{-1,1,1}) &&
            !world.ScaleObjectsWorld(selected,pivot,axes,{NAN,1,1}) && !world.ScaleObjectsWorld(selected,pivot,axes,{INFINITY,1,1}) &&
            !world.ScaleObjectsWorld(selected,pivot,axes,{1e-30f,1e-30f,1e-30f}), "zero negative nonfinite and unsupported small factors rejected");
        auto badAxes=axes; badAxes._41=5;
        Check(!world.ScaleObjectsWorld(selected,pivot,badAxes,{2,2,2}) && world.Layout().Serialize()==before, "nonrotation scale axes rejected atomically");
        history.Observe({before,state.SelectedId(),selected},{}); Check(history.CanRedo(), "rejected and no-op scaling preserves Redo");
        const auto redo=history.Target(true);
        Check(world.ReplaceLayout(SceneRuntime::SceneLayout::Parse(redo.json),root,error) && world.Layout().Serialize()==after, "Redo restores entire scaled group");
        history.Applied(true);
        Check(world.ReplaceLayout(layout,root,error), "restore scale handle fixture");
        auto scaled=child; scaled.scale={2,0.5f,1.25f};
        DirectX::XMFLOAT4X4 handle;
        std::array<float,3> factors{};
        Check(Editor::GizmoTransform::Build(world,scaled,true,handle) && Editor::GizmoTransform::ScaleDelta(world,child,handle,factors) &&
            std::abs(factors[0]-2)<0.001f && std::abs(factors[1]-0.5f)<0.001f && std::abs(factors[2]-1.25f)<0.001f,
            "scale handle extracts axis factors under inherited shear");
        auto mirrored=parent; mirrored.scale={-4,1.5f,5};
        Check(Editor::GizmoTransform::Build(world,mirrored,true,handle) && Editor::GizmoTransform::ScaleDelta(world,parent,handle,factors) &&
            std::abs(factors[0]-2)<0.001f && std::abs(factors[1]-0.5f)<0.001f, "mirrored scale handle retains signs and extracts positive ratios");
        const auto preserved=factors; handle._11=NAN;
        Check(!Editor::GizmoTransform::ScaleDelta(world,parent,handle,factors) && factors==preserved, "invalid scale handle preserves output");
        Check(world.WorldRotation("other",axes), "aligned active local axes");
        const std::array<float,3> otherPivot{poses[3]._41,poses[3]._42,poses[3]._43};
        state.RestoreSelection({"parent","other"},"other"); state.MarkSaved();
        Check(state.ScaleSelectionWorld(world,otherPivot,axes,{2,0.5f,1.25f}), "axis scale succeeds for aligned root branches");
        const auto frame=DirectX::XMLoadFloat4x4(&axes);
        const auto axisDelta=DirectX::XMMatrixTranslation(-otherPivot[0],-otherPivot[1],-otherPivot[2])*
            DirectX::XMMatrixTranspose(frame)*DirectX::XMMatrixScaling(2,0.5f,1.25f)*frame*
            DirectX::XMMatrixTranslation(otherPivot[0],otherPivot[1],otherPivot[2]);
        for (size_t index=0;index<layout.objects.size();++index)
        {
            DirectX::XMStoreFloat4x4(&expected,DirectX::XMLoadFloat4x4(&poses[index])*axisDelta);
            Check(world.WorldMatrix(layout.objects[index].id,actual) && SceneRuntime::SceneTransforms::Matches(expected,actual), "axis scale matches active oriented pivot transform");
        }
        Check(world.ReplaceLayout(layout,root,error), "restore unrepresentable scale fixture");
        state.RestoreSelection({"other","child"},"child"); state.MarkSaved();
        Check(!state.ScaleSelectionWorld(world,pivot,axes,{2,1,1}) && state.InvalidTransform() && !state.HasChanges() &&
            world.Layout().Serialize()==before && state.SelectedIds()==std::vector<std::string>{"other","child"},
            "unrepresentable shear cancels entire scale operation without partial updates");
        Check(world.ReplaceLayout(initial,root,error), "multiple scaling fixture restored");
    }

    void ValidateMultipleRotation(SceneRuntime::SceneWorld& world, const std::filesystem::path& root)
    {
        const auto initial=world.Layout();
        auto parent=initial.objects.back(); parent.id="parent"; parent.parentId.clear();
        parent.position={10,2,3}; parent.rotation={0,0.4f,0}; parent.scale={-2,2,2};
        auto child=parent; child.id="child"; child.parentId=parent.id;
        child.position={1,2,3}; child.rotation={0,0,0.3f}; child.scale={1,1,1};
        auto grand=child; grand.id="grand"; grand.parentId=child.id;
        auto other=child; other.id="other"; other.parentId.clear(); other.position={20,3,4};
        SceneRuntime::SceneLayout layout; layout.objects={grand,child,parent,other};
        std::string error;
        Check(world.ReplaceLayout(layout,root,error), "multiple rotation fixture");
        Editor::EditState state; state.Select("parent"); state.Select("other",true); state.Select("child",true);
        const auto before=world.Layout().Serialize(); const auto selected=state.SelectedIds();
        std::vector<DirectX::XMFLOAT4X4> poses;
        for (const auto& placement : layout.objects)
        {
            DirectX::XMFLOAT4X4 pose;
            Check(world.WorldMatrix(placement.id,pose), "capture group rotation world pose"); poses.push_back(pose);
        }
        const std::array<float,3> pivot{poses[1]._41,poses[1]._42,poses[1]._43};
        DirectX::XMFLOAT4X4 rotation;
        DirectX::XMStoreFloat4x4(&rotation,DirectX::XMMatrixRotationY(0.5f));
        const auto delta=DirectX::XMMatrixTranslation(-pivot[0],-pivot[1],-pivot[2])*
            DirectX::XMLoadFloat4x4(&rotation)*DirectX::XMMatrixTranslation(pivot[0],pivot[1],pivot[2]);
        Editor::EditHistory history; history.Reset({before,state.SelectedId(),selected});
        state.SetInteraction("gizmo/child/1");
        Check(state.RotateSelectionWorld(world,pivot,rotation) && state.HasChanges() && state.SelectedIds()==selected,
            "group rotation succeeds around child pivot with parent selected and mirrored scale");
        DirectX::XMFLOAT4X4 actual,expected;
        for (size_t index=0;index<layout.objects.size();++index)
        {
            DirectX::XMStoreFloat4x4(&expected,DirectX::XMLoadFloat4x4(&poses[index])*delta);
            Check(world.WorldMatrix(layout.objects[index].id,actual) && SceneRuntime::SceneTransforms::Matches(expected,actual),
                "group rotation applies exactly once and unselected descendants follow parent");
        }
        Check(world.Layout().objects[0].position==grand.position && world.Layout().objects[0].rotation==grand.rotation &&
            world.Layout().objects[1].position==child.position && world.Layout().objects[1].rotation==child.rotation,
            "selected and unselected descendants keep exact local transforms");
        const auto after=world.Layout().Serialize(); history.Observe({after,state.SelectedId(),selected},state.Interaction());
        state.BeginFrame(); history.Observe({after,state.SelectedId(),selected},{});
        const auto undo=history.Target(false);
        Check(world.ReplaceLayout(SceneRuntime::SceneLayout::Parse(undo.json),root,error), "one Undo restores rotation drag");
        history.Applied(false); state.RestoreSelection(undo.selections,undo.selection); state.SetChanged(history.Dirty(undo.json));
        Check(world.Layout().Serialize()==before && state.SelectedIds()==selected && !state.HasChanges(), "rotation Undo restores selection and clean state");
        DirectX::XMFLOAT4X4 identity; DirectX::XMStoreFloat4x4(&identity,DirectX::XMMatrixIdentity());
        Check(state.RotateSelectionWorld(world,pivot,identity) && world.Layout().Serialize()==before && !state.HasChanges(),
            "identity rotation is exact no-op");
        Check(!world.RotateObjectsWorld({},pivot,rotation) && !world.RotateObjectsWorld({"parent","missing"},pivot,rotation) &&
            !world.RotateObjectsWorld({"parent","parent"},pivot,rotation) && !world.RotateObjectsWorld(selected,{NAN,0,0},rotation),
            "empty duplicate missing selections and invalid pivot are rejected");
        auto invalid=rotation; invalid._11=NAN;
        Check(!world.RotateObjectsWorld(selected,pivot,invalid), "nonfinite rotation rejected");
        DirectX::XMStoreFloat4x4(&invalid,DirectX::XMMatrixScaling(2,1,1));
        Check(!world.RotateObjectsWorld(selected,pivot,invalid), "scale cannot be used as a group rotation");
        DirectX::XMStoreFloat4x4(&invalid,DirectX::XMMatrixScaling(-1,1,1));
        Check(!world.RotateObjectsWorld(selected,pivot,invalid), "reflection cannot be used as a group rotation");
        DirectX::XMStoreFloat4x4(&invalid,DirectX::XMMatrixTranslation(1,0,0));
        Check(!world.RotateObjectsWorld(selected,pivot,invalid) && world.Layout().Serialize()==before, "translation rejected atomically");
        history.Observe({before,state.SelectedId(),selected},{}); Check(history.CanRedo(), "no-op and rejected rotations preserve Redo");
        const auto redo=history.Target(true);
        Check(world.ReplaceLayout(SceneRuntime::SceneLayout::Parse(redo.json),root,error) && world.Layout().Serialize()==after, "Redo restores entire rotated group");
        history.Applied(true);
        Check(world.ReplaceLayout(layout,root,error), "restore local and world rotation handle fixture");
        DirectX::XMFLOAT4X4 handle,previous,extracted;
        Check(Editor::GizmoTransform::Build(world,child,false,previous), "build active rotation handle");
        const auto axes=DirectX::XMLoadFloat4x4(&previous);
        const auto turn=DirectX::XMMatrixRotationZ(DirectX::XM_PI/12);
        DirectX::XMStoreFloat4x4(&handle,axes*turn); handle._41=previous._41; handle._42=previous._42; handle._43=previous._43;
        Check(Editor::GizmoTransform::RotationDelta(world,child,handle,extracted), "extract world-axis snapped rotation delta");
        DirectX::XMStoreFloat4x4(&expected,turn);
        Check(SceneRuntime::SceneTransforms::Matches(expected,extracted), "world handle produces expected world rotation");
        auto frame=previous; frame._41=0; frame._42=0; frame._43=0;
        DirectX::XMStoreFloat4x4(&handle,turn*DirectX::XMLoadFloat4x4(&frame));
        handle._41=previous._41; handle._42=previous._42; handle._43=previous._43;
        DirectX::XMStoreFloat4x4(&expected,DirectX::XMMatrixTranspose(DirectX::XMLoadFloat4x4(&frame))*turn*DirectX::XMLoadFloat4x4(&frame));
        Check(Editor::GizmoTransform::RotationDelta(world,child,handle,extracted) && SceneRuntime::SceneTransforms::Matches(expected,extracted),
            "local-axis handle produces conjugated world rotation");
        layout.objects[2].scale={2,3,4};
        Check(world.ReplaceLayout(layout,root,error), "nonuniform parent rejection fixture");
        const auto stable=world.Layout().Serialize(); state.RestoreSelection({"other","child"},"child"); state.MarkSaved();
        DirectX::XMStoreFloat4x4(&rotation,DirectX::XMMatrixRotationZ(0.4f));
        Check(!state.RotateSelectionWorld(world,pivot,rotation) && state.InvalidTransform() && !state.HasChanges() &&
            world.Layout().Serialize()==stable && state.SelectedIds()==std::vector<std::string>{"other","child"},
            "unrepresentable local shear rejects group after earlier object without partial mutation");
        state.RestoreSelection({"parent","child"},"child");
        Check(state.RotateSelectionWorld(world,pivot,rotation), "selected nonuniform parent rotates sheared child by inheritance");
        Check(world.ReplaceLayout(initial,root,error), "multiple rotation fixture restored");
    }

    void ValidateMultipleDuplication(SceneRuntime::SceneWorld& world, const std::filesystem::path& root)
    {
        const auto initial=world.Layout();
        auto parent=initial.objects.back(); parent.id="object-1"; parent.parentId.clear();
        parent.position={10,20,30}; parent.rotation={0,0.4f,0}; parent.scale={-2,3,4};
        auto child=parent; child.id="child"; child.parentId=parent.id;
        child.position={1,2,3}; child.rotation={0,0,0.3f}; child.scale={1,1,1};
        auto grand=child; grand.id="grand"; grand.parentId=child.id;
        auto other=child; other.id="other"; other.parentId.clear();
        SceneRuntime::SceneLayout layout; layout.objects={grand,child,parent,other};
        std::string error;
        Check(world.ReplaceLayout(layout,root,error), "multiple duplication hierarchy fixture");
        Editor::EditState state; state.Select("child"); state.Select(parent.id,true); state.Select("other",true);
        const auto request=state.DuplicateSelectionRequest();
        Check(request.action==Editor::ObjectAction::Duplicate && request.ids==state.SelectedIds(), "duplicate request captures ordered selection");
        const auto before=world.Layout().Serialize();
        Editor::EditHistory history; history.Reset({before,state.SelectedId(),state.SelectedIds()});
        DirectX::XMFLOAT4X4 childBefore{},otherBefore{},actual{};
        Check(world.WorldMatrix("child",childBefore) && world.WorldMatrix("other",otherBefore), "duplicate captures original world poses");
        Check(state.DuplicateObjects(world,request.ids,{4,0,0},error), "multiple duplicate supports child before mirrored nonuniform parent");
        const auto copies=state.SelectedIds();
        Check(copies.size()==3 && state.SelectedId()==copies.back() && state.HasChanges() && world.Layout().objects.size()==7,
            "all copies selected with matching primary and no unselected descendants copied");
        const auto& added=world.Layout().objects;
        Check(added[4].parentId==copies[1] && added[4].position==child.position && added[4].rotation==child.rotation &&
            added[4].scale==child.scale && added[5].parentId.empty() && added[6].parentId.empty(), "copied hierarchy remaps parent and retains exact local SRT");
        childBefore._41+=4; otherBefore._41+=4;
        Check(world.WorldMatrix(copies[0],actual) && SceneRuntime::SceneTransforms::Matches(childBefore,actual) &&
            world.WorldMatrix(copies[2],actual) && SceneRuntime::SceneTransforms::Matches(otherBefore,actual), "all copies receive one world offset including sheared child");
        for (size_t index=0;index<layout.objects.size();++index)
            Check(added[index].id==layout.objects[index].id && added[index].position==layout.objects[index].position &&
                added[index].parentId==layout.objects[index].parentId, "original hierarchy unchanged by duplication");
        const auto after=world.Layout().Serialize(); history.Observe({after,state.SelectedId(),copies},{});
        const auto undo=history.Target(false);
        Check(world.ReplaceLayout(SceneRuntime::SceneLayout::Parse(undo.json),root,error), "one Undo restores entire multiple duplication");
        history.Applied(false); state.RestoreSelection(undo.selections,undo.selection); state.SetChanged(history.Dirty(undo.json));
        Check(world.Layout().Serialize()==before && state.SelectedIds()==request.ids && !state.HasChanges(), "Undo restores originals selection and clean state");
        Check(!state.DuplicateObjects(world,{"child","missing"},{4,0,0},error) && !error.empty() &&
            !state.DuplicateObjects(world,request.ids,{NAN,0,0},error) && !error.empty() &&
            world.Layout().Serialize()==before && state.SelectedIds()==request.ids && !state.HasChanges(), "missing ID and invalid offset leave scene and selection unchanged");
        history.Observe({before,state.SelectedId(),state.SelectedIds()},{});
        Check(history.CanRedo(), "failed multiple duplication preserves redo branch");
        const auto redo=history.Target(true);
        Check(world.ReplaceLayout(SceneRuntime::SceneLayout::Parse(redo.json),root,error), "Redo restores all copies");
        history.Applied(true); state.RestoreSelection(redo.selections,redo.selection);
        Check(world.Layout().Serialize()==after && state.SelectedIds()==copies, "Redo restores copied hierarchy and selection");
        Check(world.ReplaceLayout(layout,root,error), "restore duplicate ID fixture");
        std::vector<std::string> created;
        Check(world.DuplicateObjects({"grand","grand","other"},{4,0,0},created,error) && created.size()==2 &&
            created[0]!=created[1] && world.Layout().objects.size()==6 && world.Layout().objects[4].parentId=="child",
            "duplicate IDs normalize and unselected parent is retained");
        const auto stable=world.Layout().Serialize();
        Check(!world.DuplicateObjects({}, {4,0,0},created,error) && created.empty() && world.Layout().Serialize()==stable,
            "empty duplication rejected without mutation");
        std::string baseline;
        Check(world.DuplicateObject("other",{0,0,0},baseline,error), "ID allocation baseline");
        const auto next=std::stoull(baseline.substr(7))+1;
        Check(!world.DuplicateObjects({"other","child"},{NAN,0,0},created,error) && created.empty(), "failed batch returns no generated IDs");
        Check(world.DuplicateObjects({"other","child"},{0,0,0},created,error) && created.size()==2 &&
            created[0]=="object-"+std::to_string(next) && created[1]=="object-"+std::to_string(next+1),
            "failed batch consumes no IDs and successful batch allocates unique consecutive IDs");
        Check(world.ReplaceLayout(initial,root,error), "multiple duplication fixture restored");
    }

    void ValidateMultipleDeletion(SceneRuntime::SceneWorld& world, const std::filesystem::path& root)
    {
        const auto initial=world.Layout();
        auto parent=initial.objects.back(); parent.id="parent"; parent.parentId.clear();
        parent.position={10,20,30}; parent.rotation={0,0.4f,0}; parent.scale={-2,2,2};
        auto child=parent; child.id="child"; child.parentId=parent.id; child.position={1,2,3}; child.rotation={0,0,0.3f}; child.scale={1,1,1};
        auto grand=child; grand.id="grand"; grand.parentId=child.id;
        auto other=child; other.id="other"; other.parentId.clear();
        SceneRuntime::SceneLayout layout; layout.objects={grand,child,parent,other};
        std::string error;
        Check(world.ReplaceLayout(layout,root,error), "multiple delete hierarchy fixture");
        Editor::EditState state; state.Select("parent"); state.Select("child",true);
        const auto request=state.DeleteSelectionRequest();
        Check(request.action==Editor::ObjectAction::Delete && request.ids==std::vector<std::string>{"parent","child"},
            "delete request captures all selected IDs");
        const auto before=world.Layout().Serialize();
        DirectX::XMFLOAT4X4 grandBefore{},actual{};
        Check(world.WorldMatrix("grand",grandBefore), "multiple deletion captures surviving grandchild pose");
        Editor::EditHistory history; history.Reset({before,state.SelectedId(),state.SelectedIds()});
        Check(state.DeleteObjects(world,request.ids,error) && state.SelectedIds().empty() && state.HasChanges() &&
            world.Layout().objects.size()==2 && world.Layout().objects[0].parentId.empty() &&
            world.WorldMatrix("grand",actual) && SceneRuntime::SceneTransforms::Matches(grandBefore,actual),
            "parent and selected child deleted once while surviving grandchild becomes root at same world pose");
        const auto deleted=world.Layout().Serialize();
        history.Observe({deleted,state.SelectedId(),state.SelectedIds()},{});
        const auto undo=history.Target(false);
        Check(world.ReplaceLayout(SceneRuntime::SceneLayout::Parse(undo.json),root,error), "one Undo restores entire multiple deletion");
        history.Applied(false); state.RestoreSelection(undo.selections,undo.selection); state.SetChanged(history.Dirty(undo.json));
        Check(world.Layout().Serialize()==before && state.SelectedIds()==request.ids && !state.HasChanges(),
            "Undo restores hierarchy multi selection primary and clean state");
        Check(!state.DeleteObjects(world,{"child","missing"},error) && !error.empty() &&
            world.Layout().Serialize()==before && state.SelectedIds()==request.ids && !state.HasChanges(),
            "missing later ID cancels deletion without changing state or selection");
        history.Observe({before,state.SelectedId(),state.SelectedIds()},{});
        Check(history.CanRedo(), "failed multiple deletion preserves redo branch");
        const auto redo=history.Target(true);
        Check(world.ReplaceLayout(SceneRuntime::SceneLayout::Parse(redo.json),root,error) && world.Layout().Serialize()==deleted,
            "Redo reapplies entire multiple deletion");
        history.Applied(true);
        Check(world.ReplaceLayout(layout,root,error), "restore multiple deletion duplicate IDs fixture");
        state.RestoreSelection({"parent","child","other"},"other"); state.MarkSaved();
        Check(state.DeleteObjects(world,{"parent","child","child"},error) && state.SingleSelection() && state.SelectedId()=="other" &&
            world.Layout().objects.size()==2, "duplicate IDs normalize and unrelated current selection remains selected");
        layout.objects[2].scale={2,3,4};
        Check(world.ReplaceLayout(layout,root,error), "unrepresentable surviving grandchild fixture");
        const auto invalid=world.Layout().Serialize(); state.RestoreSelection(request.ids,"child"); state.MarkSaved();
        Check(!state.DeleteObjects(world,request.ids,error) && error.find("grand")!=std::string::npos &&
            world.Layout().Serialize()==invalid && state.SelectedIds()==request.ids && !state.HasChanges(),
            "unrepresentable surviving descendant cancels every selected deletion atomically");
        Check(state.DeleteObjects(world,{"parent","child","grand"},error) && world.Layout().objects.size()==1 &&
            world.Layout().objects.front().id=="other" && state.SelectedIds().empty(),
            "selected sheared subtree can be removed without decomposing already deleted nodes");
        Check(!world.RemoveObjects({},error) && !error.empty(), "empty deletion is rejected");
        Check(world.RemoveObjects({"other"},error) && world.Layout().objects.empty(), "all scene objects can be deleted");
        Check(world.ReplaceLayout(initial,root,error), "multiple deletion fixture restored");
    }

    void ValidateInheritedRendering(Engine::DirectX12Renderer& renderer, const std::filesystem::path& root)
    {
        SceneRuntime::SceneWorld world;
        std::string error,created;
        Check(world.Initialize(renderer,root,root/"Assets/Scenes/TitleStreet.json",root/"Shaders/Mesh.hlsl"),
            "inherited transform renderer initializes");
        auto parent=world.Layout().objects.front();
        parent.id="parent"; parent.parentId.clear(); parent.position={10,20,30};
        parent.rotation={0,DirectX::XM_PIDIV2,0}; parent.scale={2,3,4};
        auto child=parent; child.id="child"; child.parentId=parent.id;
        child.position={1,2,3}; child.rotation={0,0,0.37f}; child.scale={1,1,1};
        auto grandchild=child; grandchild.id="grandchild"; grandchild.parentId=child.id; grandchild.position={0,1,0};
        SceneRuntime::SceneLayout layout;
        layout.objects={grandchild,child,parent};
        Check(world.ReplaceLayout(SceneRuntime::SceneLayout::Parse(layout.Serialize()),root,error),
            "explicit local scene loads and preserves coordinate convention");
        std::vector<DirectX::XMFLOAT4X4> expected;
        Check(SceneRuntime::SceneTransforms::Resolve(layout,expected,error), "inherited expected matrices");
        DirectX::XMFLOAT4X4 matrix;
        Check(world.WorldMatrix("child",matrix) && SceneRuntime::SceneTransforms::Matches(matrix,expected[1]),
            "draw object receives full inherited matrix including shear");
        std::array<std::array<float,3>,8> before{},after{};
        Check(world.WorldBounds("grandchild",before), "inherited bounds before edit");
        auto moved=parent.position; moved[0]+=5;
        Check(world.SetLocalTransform(parent.id,moved,parent.rotation,parent.scale) && world.WorldBounds("grandchild",after),
            "parent edit updates all descendant draw bounds");
        for (size_t corner=0;corner<8;++corner)
            Check(std::abs(after[corner][0]-before[corner][0]-5)<0.01f &&
                std::abs(after[corner][1]-before[corner][1])<0.01f, "descendant bounds follow parent displacement");
        const auto snapshot=world.Layout().Serialize();
        Check(!world.SetParent("child",{},error) && !error.empty() && world.Layout().Serialize()==snapshot &&
            !world.RemoveObjects({"parent"},error) && world.Layout().Serialize()==snapshot,
            "unrepresentable shear on detach or delete preserves scene atomically");
        auto invalid=moved; invalid[0]=(std::numeric_limits<float>::max)();
        auto huge=parent.scale; huge[0]=(std::numeric_limits<float>::max)();
        Check(!world.SetLocalTransform("parent",invalid,parent.rotation,huge) && world.Layout().Serialize()==snapshot,
            "overflowing inherited edit preserves placements and matrices");
        Check(world.TranslateObjectsWorld({"parent","child","grandchild"},{3,0,0}) && world.WorldBounds("grandchild",before),
            "group parent and descendants move together");
        for (size_t corner=0;corner<8;++corner)
            Check(std::abs(before[corner][0]-after[corner][0]-3)<0.01f, "group inheritance avoids double translation");
        Check(world.WorldMatrix("child",matrix), "child draw matrix before world move");
        Check(world.TranslateObjectsWorld({"child"},{0,0,2}), "child world move converts through rotated scaled parent");
        DirectX::XMFLOAT4X4 shifted;
        Check(world.WorldMatrix("child",shifted) && std::abs(shifted._43-matrix._43-2)<0.001f &&
            std::abs(shifted._41-matrix._41)<0.001f, "child movement uses world axes");
        Check(world.DuplicateObject("child",{2,0,0},created,error) && world.WorldMatrix(created,shifted) &&
            std::abs(shifted._41-matrix._41-2)<0.001f, "duplicate offset uses world axes under transformed parent");
        auto addition=child; addition.id="added";
        Check(world.AddObject(addition,root,created,error) && world.WorldMatrix(created,matrix), "new child gets inherited draw matrix");
        Engine::Camera camera;
        AuthoredViewFixture::SetHome(camera); AuthoredViewFixture::SetProjection(camera,1);
        Check(renderer.Render({0,0,0,1},[&](ID3D12GraphicsCommandList* commands,float) { world.Draw(commands,camera,{}); })!=Engine::RenderResult::Failed,
            "full inherited matrices render through model pipeline");
        Check(renderer.WaitForIdle(), "inherited rendering completes before resource destruction");
        layout.objects[2].scale={2,2,2}; layout.objects[1].rotation={0,0,0};
        Check(world.ReplaceLayout(layout,root,error) && world.WorldMatrix("child",matrix), "uniform parent detach fixture");
        Check(world.SetParent("child",{},error) && world.WorldMatrix("child",shifted) &&
            SceneRuntime::SceneTransforms::Matches(matrix,shifted), "representable reparent preserves world pose");
        Check(world.SetParent("child","parent",error) && world.RemoveObjects({"parent"},error) && world.WorldMatrix("child",shifted) &&
            SceneRuntime::SceneTransforms::Matches(matrix,shifted), "representable parent deletion preserves child pose");
        Check(world.ReplaceLayout(layout,root,error), "inherited serialization fixture restored");
        const auto json=world.Layout().Serialize();
        const auto path=std::filesystem::absolute("generated/tests/local-scene.json");
        world.Layout().Save(path);
        Check(world.Reload(root,path,error) && world.Layout().Serialize()==json, "local scene survives save and reload");
        ValidateTransformApi(world,root);
        ValidateHierarchyMutations(world,root);
        ValidateGizmoTransforms(world,root);
        ValidateEditTransactions(world,root);
        ValidateTransformWorkflow(renderer,world,root);
        ValidateMultipleDeletion(world,root);
        ValidateMultipleDuplication(world,root);
        ValidateMultipleRotation(world,root);
        ValidateMultipleScaling(world,root);
        CheckGpuMessages(renderer.GetDevice());
    }

#if defined(_DEBUG)
    void ValidateAssetPreview(Engine::DirectX12Renderer& renderer)
    {
        const auto root=std::filesystem::absolute("Content");
        Editor::AssetPreview preview;
        const Editor::ProjectAsset model{"Assets/Models/Title/Surface/Commercial/building-k.obj",Editor::AssetKind::Model};
        preview.Request(model);
        Check(preview.Prepare(renderer,root) && preview.Ready() && preview.Error().empty(), "independent model preview loads");
        Check(renderer.Render({0,0,0,1},[&](ID3D12GraphicsCommandList* commands,float) {
            Check(preview.Render(commands), "model preview renders to dedicated target");
        },[&]() { ImGui::Begin("Asset preview test"); preview.Draw(model); ImGui::End(); })!=Engine::RenderResult::Failed,
            "model preview is sampled by ImGui after rendering");
        const Editor::ProjectAsset image{"Assets/Models/Title/Surface/Textures/plaster.png",Editor::AssetKind::Texture};
        preview.Request(image);
        Check(preview.Prepare(renderer,root) && preview.Ready() && preview.Error().empty(), "image preview loads and replaces model resource");
        Check(renderer.Render({0,0,0,1},{},[&]() { ImGui::Begin("Asset preview test"); preview.Draw(image); ImGui::End(); })!=Engine::RenderResult::Failed,
            "image preview dimensions and pixels render in Inspector");
        preview.Request({"Assets/missing.png",Editor::AssetKind::Texture});
        Check(preview.Prepare(renderer,root) && !preview.Ready() && !preview.Error().empty(), "missing preview reports failure without displaying another asset");
        Check(renderer.WaitForIdle(), "asset preview GPU completion before releasing resources");
    }
#endif

    void ValidateRenderTexture()
    {
        Engine::Window window;
        Engine::DirectX12Renderer renderer;
        Check(window.Create(L"Hidden render texture validation", 320, 240), "render texture window");
        Check(renderer.Initialize(window.GetHandle()), "render texture renderer");
        Engine::RenderTexture target;
        Engine::RenderTexture gameTarget;
#if defined(_DEBUG)
        SceneRuntime::SceneWorld hierarchyWorld;
        const auto hierarchyRoot=std::filesystem::absolute("Content");
        Check(hierarchyWorld.Initialize(renderer,hierarchyRoot,hierarchyRoot/"Assets/Scenes/TitleStreet.json",
            hierarchyRoot/"Shaders/Mesh.hlsl"), "Hierarchy panel test world loads");
        auto nested=hierarchyWorld.Layout();
        nested.objects[2].parentId=nested.objects[1].id;
        nested.objects[1].parentId=nested.objects[0].id;
        std::string hierarchyError;
        Check(hierarchyWorld.ReplaceLayout(nested,hierarchyRoot,hierarchyError), "Hierarchy panel contains nested rows");
        Editor::ObjectPanel objectPanel;
        Editor::EditState hierarchyState;
#endif
#if defined(_DEBUG)
        ValidateAssetPreview(renderer);
        Editor::PanelLayout::Initialize(std::filesystem::absolute("generated/tests/editor-layout/layout.ini"));
        Editor::PanelLayout::Reset();
        UINT64 stableTextureId = 0, stableGameId = 0;
#endif
        Check(!target.Begin(nullptr,{0,0,0,1}) && !target.End(nullptr), "uninitialized target rejects recording");
        for (const auto size : std::array<std::array<UINT,2>,3>{{{64,32},{32,64},{64,32}}})
        {
            Check(target.Resize(renderer,size[0],size[1]), "render texture creates and resizes");
            auto* resource = target.GetResource();
            Check(target.Resize(renderer,size[0],size[1]) && target.GetResource()==resource,
                "same size keeps render texture and descriptors");
            Check(!target.Resize(renderer,0,0) && !target.Resize(renderer,16385,32) &&
                target.GetResource()==resource && target.GetWidth()==size[0] && target.GetHeight()==size[1],
                "hidden or invalid size preserves last valid target");
            Check(target.GetShaderResourceView().ptr!=0, "render texture exposes copyable SRV");
#if defined(_DEBUG)
            const auto textureId = renderer.SetSceneTexture(target.GetShaderResourceView()).ptr;
            Check(textureId && (!stableTextureId || textureId==stableTextureId), "Scene UI descriptor remains stable after resize");
            stableTextureId = textureId;
            Check(gameTarget.Resize(renderer,size[1],size[0]), "independent Game render texture resizes");
            const auto gameId=renderer.SetSceneTexture(gameTarget.GetShaderResourceView(),1).ptr;
            Check(gameId && gameId!=textureId && (!stableGameId || gameId==stableGameId),
                "Game descriptor is distinct from Scene and stable across resize");
            stableGameId=gameId;
            const auto modelPreviewId=renderer.SetSceneTexture(target.GetShaderResourceView(),2).ptr;
            const auto imagePreviewId=renderer.SetSceneTexture(gameTarget.GetShaderResourceView(),3).ptr;
            Check(modelPreviewId && imagePreviewId && modelPreviewId!=imagePreviewId &&
                modelPreviewId!=textureId && imagePreviewId!=gameId,
                "asset previews use dedicated UI descriptors independent of Scene and Game");
            Check(!renderer.SetSceneTexture(target.GetShaderResourceView(),4).ptr, "invalid preview slot is rejected");
#endif
            auto* device = renderer.GetDevice();
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
            UINT64 bytes=0;
            const auto description=resource->GetDesc();
            device->GetCopyableFootprints(&description,0,1,0,&footprint,nullptr,nullptr,&bytes);
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type=D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC buffer{};
            buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
            buffer.Width=bytes;
            buffer.Height=buffer.DepthOrArraySize=buffer.MipLevels=1;
            buffer.SampleDesc.Count=1;
            buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            ComPtr<ID3D12Resource> readback;
            Hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,IID_PPV_ARGS(&readback)));
            for (int frame=0;frame<2;++frame)
            {
                const std::array<float,4> color{float(frame==0),float(frame==1),0,1};
                Check(renderer.Render({0,0,0,1},[&](ID3D12GraphicsCommandList* commands,float)
                {
                    Check(target.Begin(commands,color) && !target.Begin(commands,color), "render texture binds color and depth once");
                    Check(!target.Resize(renderer,16,16), "resize rejected during target recording");
                    Check(target.End(commands) && !target.End(commands), "render texture ends once for shader sampling");
#if defined(_DEBUG)
                    Check(gameTarget.Begin(commands,{0,0,1,1}) && gameTarget.End(commands),
                        "Game texture renders independently before Scene readback");
#endif
                    D3D12_RESOURCE_BARRIER barrier{};
                    barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                    barrier.Transition.pResource=resource;
                    barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                    barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                    barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
                    commands->ResourceBarrier(1,&barrier);
                    D3D12_TEXTURE_COPY_LOCATION source{},destination{};
                    source.pResource=resource;
                    source.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                    destination.pResource=readback.Get();
                    destination.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                    destination.PlacedFootprint=footprint;
                    commands->CopyTextureRegion(&destination,0,0,0,&source,nullptr);
                    std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);
                    commands->ResourceBarrier(1,&barrier);
                }
#if defined(_DEBUG)
                , [&]()
                {
                    Editor::PanelLayout::BeginFrame();
                    ImGui::SetNextWindowFocus();
                    objectPanel.Draw(hierarchyWorld,hierarchyState,true);
                    Editor::ScenePanel panel;
                    ImGui::SetNextWindowFocus();
                    panel.Begin(textureId);
                    Check(frame==0 || (panel.Viewport().Valid() && panel.RequestedSize()[0]>0), "docked Scene image has usable content rectangle");
                    Check(ImGui::GetWindowDockID()!=0, "Scene belongs to the standard dockspace");
                    Editor::ScenePanel::End();
                    Editor::ScenePanel gamePanel;
                    gamePanel.Begin(gameId,"Game (title composition)###Game");
                    Check(ImGui::GetWindowDockID()!=0, "Game belongs to the standard dockspace");
                    Editor::ScenePanel::End();
                }
#endif
                )!=Engine::RenderResult::Failed, "offscreen frame submitted");
                Check(renderer.WaitForIdle(), "offscreen copy GPU completion");
                void* mapped=nullptr;
                const D3D12_RANGE range{0,static_cast<SIZE_T>(bytes)};
                Hr(readback->Map(0,&range,&mapped));
                const auto* pixels=static_cast<const unsigned char*>(mapped);
                const size_t last=static_cast<size_t>(size[1]-1)*footprint.Footprint.RowPitch+(size[0]-1)*4;
                const bool expected=pixels[frame]==255 && pixels[1-frame]==0 && pixels[3]==255 &&
                    pixels[last+frame]==255 && pixels[last+1-frame]==0 && pixels[last+3]==255;
                const D3D12_RANGE written{0,0};
                readback->Unmap(0,&written);
                Check(expected, "resized offscreen target contains current clear color at both corners");
            }
        }
#if defined(_DEBUG)
        Check(Editor::PanelLayout::Save(), "Editor layout saved to test directory");
        std::ifstream layoutFile("generated/tests/editor-layout/layout.ini");
        const std::string savedLayout((std::istreambuf_iterator<char>(layoutFile)), {});
        Check(savedLayout.find("[Docking][Data]")!=std::string::npos && savedLayout.find("[Window][Scene]")!=std::string::npos,
            "layout stores docking and Scene settings");
        Editor::PanelLayout::Initialize(std::filesystem::absolute("generated/tests/editor-layout/layout.ini"));
#endif
        Check(renderer.WaitForIdle(), "render texture destruction GPU completion");
        CheckGpuMessages(renderer.GetDevice());
    }

    void ValidateParentOperations(Engine::DirectX12Renderer& renderer, SceneRuntime::SceneWorld& world,
        const std::filesystem::path& root)
    {
        const auto initial=world.Layout();
        auto layout=initial;
        layout.objects[1].parentId=layout.objects[0].id;
        std::string error,created;
        Check(renderer.WaitForIdle() && world.ReplaceLayout(layout,root,error), "parent data loads into live world");
        const auto child=world.Layout().objects[1];
        DirectX::XMFLOAT4X4 childWorld{},resultWorld{};
        Check(world.WorldMatrix(child.id,childWorld), "reparent captures child world pose");
        Editor::EditState parentState;
        parentState.Select(child.id);
        Check(parentState.SetParent(world,child.id,{},error) && parentState.HasChanges() &&
            parentState.SelectedId()==child.id && world.WorldMatrix(child.id,resultWorld) &&
            SceneRuntime::SceneTransforms::Matches(childWorld,resultWorld),
            "reparent to root preserves placement and selection while marking changes");
        parentState.MarkSaved();
        Check(parentState.SetParent(world,child.id,{},error) && !parentState.HasChanges(), "unchanged parent does not mark changes");
        Check(parentState.SetParent(world,child.id,child.parentId,error), "child can be reparented to existing root");
        const auto parentSnapshot=world.Layout().Serialize();
        parentState.MarkSaved();
        Check(!parentState.SetParent(world,child.parentId,child.id,error) && !error.empty() &&
            !parentState.HasChanges() && world.Layout().Serialize()==parentSnapshot, "reparent to descendant is rejected atomically");
        Check(!parentState.SetParent(world,child.id,child.id,error) && !parentState.SetParent(world,child.id,"missing-parent",error) &&
            !parentState.SetParent(world,"missing-object",{},error) && world.Layout().Serialize()==parentSnapshot,
            "reparent rejects self and missing IDs without changing world");
        Editor::EditHistory reparentHistory;
        reparentHistory.Reset({parentSnapshot,child.id,{child.id}});
        Check(parentState.SetParent(world,child.id,{},error), "reparent Undo fixture moves child to root");
        reparentHistory.Observe({world.Layout().Serialize(),child.id,{child.id}},{});
        const auto reparented=world.Layout().Serialize();
        Check(world.ReplaceLayout(SceneRuntime::SceneLayout::Parse(reparentHistory.Target(false).json),root,error), "Undo restores previous parent");
        reparentHistory.Applied(false);
        Check(world.Layout().Serialize()==parentSnapshot &&
            world.ReplaceLayout(SceneRuntime::SceneLayout::Parse(reparentHistory.Target(true).json),root,error), "Redo restores changed parent");
        Check(world.Layout().Serialize()==reparented && world.SetParent(child.id,child.parentId,error), "reparent history fixture restores parent");
        auto added=child;
        added.id="parent-added";
        Check(world.AddObject(added,root,created,error) && world.Layout().objects.back().parentId==child.parentId,
            "adding a child validates its parent against the whole scene");
        Check(world.DuplicateObject(child.id,{4,0,0},created,error) && world.Layout().objects.back().parentId==child.parentId,
            "duplicate preserves parent relation");
        const auto before=world.Layout().Serialize();
        auto missing=child;
        missing.id="invalid-parent-added";
        missing.parentId="missing-parent";
        Check(!world.AddObject(missing,root,created,error) && world.Layout().Serialize()==before,
            "invalid child addition preserves whole world");
        auto cyclic=world.Layout();
        cyclic.objects[0].parentId=child.id;
        Check(!world.ReplaceLayout(cyclic,root,error) && world.Layout().Serialize()==before,
            "cyclic scene replacement preserves loaded world");
        Editor::EditHistory history;
        history.Reset({before,child.id,{child.id}});
        Check(world.RemoveObjects({world.Layout().objects[1].parentId},error), "parent object deletion supports an aliased parent ID");
        for (const auto& object : world.Layout().objects)
            Check(object.parentId.empty(), "deleting parent makes direct children roots");
        const auto found=std::find_if(world.Layout().objects.begin(),world.Layout().objects.end(),
            [&](const auto& object) { return object.id==child.id; });
        Check(found!=world.Layout().objects.end() && world.WorldMatrix(child.id,resultWorld) &&
            SceneRuntime::SceneTransforms::Matches(childWorld,resultWorld), "parent deletion preserves child world transform");
        history.Observe({world.Layout().Serialize(),child.id,{child.id}},{});
        Check(world.ReplaceLayout(SceneRuntime::SceneLayout::Parse(history.Target(false).json),root,error) &&
            world.Layout().Serialize()==before, "Undo snapshot restores parent and child relationships");
        Check(world.ReplaceLayout(initial,root,error), "parent operation fixture restores original world");
    }

    void ValidateGroupMove(Engine::DirectX12Renderer& renderer, SceneRuntime::SceneWorld& world,
        const std::filesystem::path& root)
    {
        const auto initial=world.Layout();
        const auto json=initial.Serialize();
        Editor::EditState state;
        state.Select("pick-far");
        state.Select("pick-near",true);
        const auto selected=state.SelectedIds();
        Editor::EditHistory history;
        history.Reset({json,state.SelectedId(),selected});
        std::array<std::array<float,3>,8> before,after;
        Check(world.WorldBounds("pick-near",before), "group move initial draw bounds");
        Engine::Camera focusCamera;
        AuthoredViewFixture::SetProjection(focusCamera,1.5f);
        focusCamera.SetRotation(0.4f,0.2f);
        DirectX::XMFLOAT4X4 direction;
        DirectX::XMStoreFloat4x4(&direction,focusCamera.GetViewMatrix());
        direction._41=0; direction._42=0; direction._43=0;
        const auto focused=Editor::FocusPosition(world,selected,focusCamera);
        Check(focused.has_value(), "multiple selection focus computes shared camera position");
        focusCamera.SetPosition(*focused);
        const auto viewProjection=focusCamera.GetViewProjectionMatrix();
        for (const auto& id : selected)
        {
            std::array<std::array<float,3>,8> bounds;
            Check(world.WorldBounds(id,bounds), "multiple focus uses world bounds");
            for (const auto& corner : bounds)
            {
                DirectX::XMFLOAT3 projected;
                DirectX::XMStoreFloat3(&projected,DirectX::XMVector3TransformCoord(
                    DirectX::XMVectorSet(corner[0],corner[1],corner[2],1),DirectX::XMLoadFloat4x4(&viewProjection)));
                Check(std::abs(projected.x)<=1 && std::abs(projected.y)<=1 && projected.z>=0 && projected.z<=1,
                    "all selected mirrored object corners fit after focus");
            }
        }
        DirectX::XMFLOAT4X4 afterDirection;
        DirectX::XMStoreFloat4x4(&afterDirection,focusCamera.GetViewMatrix());
        afterDirection._41=0; afterDirection._42=0; afterDirection._43=0;
        Check(SceneRuntime::SceneTransforms::Matches(direction,afterDirection) && !Editor::FocusPosition(world,{},focusCamera) &&
            !Editor::FocusPosition(world,{selected.front(),"missing"},focusCamera) && world.Layout().Serialize()==json,
            "focus preserves viewing direction and scene, and rejects empty or missing selection");

        const std::array<float,3> delta{2,-3,1};
        Check(state.TranslateSelectionWorld(world,delta) && state.HasChanges() && state.SelectedIds()==selected,
            "group move preserves multi selection and marks changes");
        for (size_t index=0;index<initial.objects.size();++index)
        {
            const auto& old=initial.objects[index];
            const auto& moved=world.Layout().objects[index];
            Check(moved.rotation==old.rotation && moved.scale==old.scale && moved.Model()==old.Model(),
                "group move preserves rotation, scale and model");
            for (size_t axis=0;axis<3;++axis)
                Check(moved.position[axis]==old.position[axis]+delta[axis], "group move applies identical world delta");
        }
        Check(world.WorldBounds("pick-near",after), "group move updated draw bounds");
        for (size_t corner=0;corner<before.size();++corner)
            for (size_t axis=0;axis<3;++axis)
                Check(std::abs(after[corner][axis]-before[corner][axis]-delta[axis])<0.001f,
                    "group move updates rendered transform and mirrored bounds");
        Check(world.PickRay({1.6f,-2.4f,0},{0,0,1}).value_or("")=="pick-near", "group move updates picking transform");
        history.Observe({world.Layout().Serialize(),state.SelectedId(),selected},"drag");
        Check(state.TranslateSelectionWorld(world,{0.5f,0,0}), "second continuous group move applies");
        const auto movedJson=world.Layout().Serialize();
        history.Observe({movedJson,state.SelectedId(),selected},{});
        Check(history.CanUndo(), "group drag creates an Undo entry");
        std::string error;
        const auto undo=history.Target(false);
        Check(renderer.WaitForIdle() && world.ReplaceLayout(SceneRuntime::SceneLayout::Parse(undo.json),root,error), "group move Undo restores world");
        history.Applied(false);
        state.RestoreSelection(undo.selections,undo.selection);
        Check(world.Layout().Serialize()==json && !history.CanUndo() && state.SelectedIds()==selected,
            "one Undo reverses full group drag and preserves selection");
        const auto redo=history.Target(true);
        Check(world.ReplaceLayout(SceneRuntime::SceneLayout::Parse(redo.json),root,error), "group move Redo restores world");
        history.Applied(true);
        Check(world.Layout().Serialize()==movedJson, "Redo restores entire group movement");
        Check(!world.TranslateObjectsWorld({"pick-far","missing"},delta) && world.Layout().Serialize()==movedJson,
            "invalid later ID cannot leave earlier object partially moved");
        Check(!world.TranslateObjectsWorld({"pick-far","pick-far"},delta) && world.Layout().Serialize()==movedJson,
            "duplicate group IDs are rejected without double moving");
        Check(!state.TranslateSelectionWorld(world,{NAN,0,0}) && state.InvalidTransform() && world.Layout().Serialize()==movedJson,
            "nonfinite group move is rejected atomically");
        state.SetChanged(false);
        Check(state.TranslateSelectionWorld(world,{}) && !state.HasChanges(), "zero group delta does not mark changes");
        const float maximum=(std::numeric_limits<float>::max)();
        Check(world.SetLocalTransform("pick-far",{0,0,10},{0,0,0},{1,1,1}) &&
            world.SetLocalTransform("pick-near",{maximum,0,5},{0,0,0},{1,1,1}), "finite extreme group move fixture");
        const auto extremeJson=world.Layout().Serialize();
        Check(!world.TranslateObjectsWorld(selected,{maximum,0,0}) && world.Layout().Serialize()==extremeJson,
            "overflow in later move rejects earlier valid translation atomically");
        Check(world.ReplaceLayout(initial,root,error), "group move fixture restores initial placements");
    }

    void ValidateSceneDocument(Engine::DirectX12Renderer& renderer, const std::filesystem::path& content)
    {
        const auto root=std::filesystem::absolute("generated/tests/scene-document");
        std::filesystem::create_directories(root/"Assets/Scenes");
        const auto initial=root/"Assets/Scenes/initial.json";
        const auto opened=root/"Assets/Scenes/opened.json";
        const auto created=root/"Assets/Scenes/created.json";
        if (std::filesystem::exists(created)) std::filesystem::remove(created);
        auto layout=SceneRuntime::SceneLayout::Load(content/"Assets/Scenes/TitleStreet.json");
        layout.Save(initial);
        auto alternate=layout;
        alternate.objects.resize(1); alternate.settings.mainCamera.clear();
        alternate.Save(opened);
        Editor::SceneDocument document(initial);
        SceneRuntime::SceneWorld world;
        Check(world.Initialize(renderer,content,initial,content/"Shaders/Mesh.hlsl"), "scene document initial world loads");
        const auto original=world.Layout().Serialize();
        std::string error;
        Check(document.Request(opened,false,true) && document.NeedsConfirmation() && !document.Ready(),
            "dirty scene switch awaits confirmation");
        Check(!document.Apply(world,content,error) && document.Path()==initial && world.Layout().Serialize()==original,
            "unconfirmed switch preserves active scene");
        document.Cancel();
        Check(!document.Pending() && document.Path()==initial, "cancel preserves document and scene");
        Check(document.Request(root/"missing.json",false,false), "clean scene switch queues directly");
        Check(!document.Apply(world,content,error) && !error.empty() && document.Path()==initial &&
            world.Layout().Serialize()==original, "failed scene read preserves path and world");
        Check(document.Request(opened,false,true), "scene switch can be retried after failure");
        document.Confirm();
        Check(renderer.WaitForIdle() && document.Apply(world,content,error) && document.Path()==opened &&
            !document.UnsavedNew() && world.Layout().objects.size()==1, "confirmed open changes scene and save destination");
        Check(document.Request(created,true,false) && renderer.WaitForIdle() && document.Apply(world,content,error),
            "new scene replaces world after GPU completion");
        Check(document.UnsavedNew() && document.Path()==created && world.Layout().objects.empty() &&
            !std::filesystem::exists(created), "empty new scene remains unsaved without writing a file");
        alternate.Save(created);
        bool saveRejected=false;
        try { document.Save(world.Layout()); }
        catch (const std::exception&) { saveRejected=true; }
        Check(saveRejected && document.UnsavedNew() && SceneRuntime::SceneLayout::Load(created).objects.size()==1,
            "new scene refuses to overwrite a destination created while editing");
        std::filesystem::remove(created);
        document.Save(world.Layout());
        Check(!document.UnsavedNew() && SceneRuntime::SceneLayout::Load(created).objects.empty(),
            "saving new scene writes its own destination");
        Check(document.Request(initial,true,false) && !document.Apply(world,content,error) && document.Path()==created,
            "new scene rejects existing destination without replacing current document");
        for (const auto& name : {std::string("../escape.json"),std::string("bad.txt"),std::string("initial.json"),std::string("bad:name.json")})
        {
            bool rejected=false;
            try { static_cast<void>(Editor::SceneDocument::NewTarget(root,name)); }
            catch (const std::exception&) { rejected=true; }
            Check(rejected, "new scene rejects traversal, invalid names and existing files");
        }
        Check(Editor::SceneDocument::NewTarget(root,"新規シーン.JSON").parent_path()==root/"Assets/Scenes",
            "new scene accepts UTF-8 filename and uppercase extension");
        Check(renderer.WaitForIdle(), "scene document destruction GPU completion");
    }

    void ValidatePlaySnapshot(Engine::DirectX12Renderer& renderer, const std::filesystem::path& root)
    {
        SceneRuntime::SceneWorld world;
        std::string error;
        Check(world.Initialize(renderer,root,root/"Assets/Scenes/TitleStreet.json",root/"Shaders/Mesh.hlsl"), "snapshot edit world");
        Editor::EditState state;
        const auto first=world.Layout().objects.front().id, last=world.Layout().objects.back().id;
        state.RestoreSelection({first,last},last);
        Editor::EditHistory history; const auto saved=world.Layout().Serialize();
        history.Reset({saved,last,state.SelectedIds()});
        Check(state.Rename(world,last,"First edit"), "snapshot first edit");
        const auto edited=world.Layout().Serialize(); history.Observe({edited,last,state.SelectedIds()},{});
        Check(state.Rename(world,last,"Redo edit"), "snapshot redo branch edit");
        history.Observe({world.Layout().Serialize(),last,state.SelectedIds()},{});
        const auto undo=history.Target(false);
        Check(world.ReplaceLayout(SceneRuntime::SceneLayout::Parse(undo.json),root,error), "snapshot establish redo branch");
        history.Applied(false); state.SetChanged(history.Dirty(edited));
        Editor::SceneDocument document(root/"Assets/Scenes/TitleStreet.json");
        const auto originalPath=document.Path();
        Editor::PlaySnapshot snapshot(world,state,history,document);
        const auto redoJson=history.Target(true).json;
        Check(state.Rename(world,last,"Temporary runtime mutation"), "simulate edit world mutation during play");
        const auto changed=world.Layout().Serialize();
        state.Select(first); state.MarkSaved(); history.Reset({changed,first});
        document=Editor::SceneDocument(root/"Assets/Scenes/Temporary.json");
        Check(!snapshot.Restore(world,root/"missing-assets",state,history,document,error) && !error.empty() &&
            world.Layout().Serialize()==changed && state.SingleSelection() && state.SelectedId()==first && !state.HasChanges() &&
            !history.CanUndo() && !history.CanRedo() && document.Path()!=originalPath, "failed Stop restoration leaves all current editor data intact");
        Check(snapshot.Restore(world,root,state,history,document,error) && error.empty() && world.Layout().Serialize()==edited &&
            state.SelectedIds()==std::vector<std::string>{first,last} && state.SelectedId()==last && state.HasChanges() &&
            document.Path()==originalPath && history.CanUndo() && history.CanRedo() && history.Target(true).json==redoJson && history.Dirty(edited),
            "Stop restores layout document selection dirty baseline and complete Undo Redo branch");
        Check(snapshot.Restore(world,root/"missing-assets",state,history,document,error), "unchanged world restores metadata without asset reload");
        Editor::SceneDocument fresh(root/"Assets/Scenes/PlaySnapshotNew.json");
        fresh.Request(fresh.Path(),true,false);
        Check(fresh.Apply(world,root,error) && fresh.UnsavedNew(), "unsaved new document snapshot fixture");
        state.Reloaded(); state.SetChanged(true); history.Reset({world.Layout().Serialize(),{}});
        Editor::PlaySnapshot newSnapshot(world,state,history,fresh);
        fresh=Editor::SceneDocument(originalPath); state.MarkSaved();
        Check(newSnapshot.Restore(world,root,state,history,fresh,error) && fresh.UnsavedNew() && state.HasChanges() &&
            fresh.Path().filename()=="PlaySnapshotNew.json" && !std::filesystem::exists(fresh.Path()), "Stop restores unsaved new scene identity without writing a file");
    }

    void ValidateGameSession(Engine::DirectX12Renderer& renderer, const std::filesystem::path& root)
    {
        auto layout=SceneRuntime::SceneLayout::Load(root/"Assets/Scenes/TitleStreet.json");
        layout.objects.back().name="Unsaved runtime name";
        layout.objects.back().position[0]+=3;
        const auto unsaved=layout.Serialize();
        const auto onDisk=SceneRuntime::SceneLayout::Load(root/"Assets/Scenes/TitleStreet.json").Serialize();
        Editor::GameSession session;
        std::string error;
        Check(!session.Step() && !session.Update(0.1,true) && !session.Pause() && !session.Stop(), "editing has no running game session");
        Check(session.Play(renderer,root,layout,error) && error.empty() && session.Runtime() &&
            session.Runtime()->World().Layout().Serialize()==unsaved, "Play uses unsaved layout snapshot rather than saved title scene");
        const auto* running=session.Runtime();
        const auto initialMote=running->Particle(0);
        const auto initialCamera=running->CameraPosition();
        layout.objects.back().position[0]+=100;
        Check(running->World().Layout().Serialize()==unsaved, "runtime layout is independent from later editor changes");
        Check(session.Update(0.1,true) && session.State().Elapsed()==0.1 && session.State().Updates()==1 &&
            running->Particle(0)!=initialMote && running->CameraPosition()!=initialCamera,
            "accepted Play tick advances shared background runtime");
        const auto mote=running->Particle(0); const auto camera=running->CameraPosition();
        Check(!session.Update(0.1,false) && !session.Update(NAN,true) && running->Particle(0)==mote &&
            running->CameraPosition()==camera && session.State().Updates()==1, "inactive and invalid ticks do not change runtime or clock");
        Check(!session.Step() && !session.Play(renderer,root,layout,error) && session.Runtime()==running, "repeated Play preserves active runtime");
        Check(session.Pause() && !session.Update(0.1,true) && running->Particle(0)==mote &&
            running->CameraPosition()==camera, "Pause freezes actual runtime camera and particles");
        Engine::RenderTexture target;
        Check(target.Resize(renderer,320,180), "Game render texture created");
        const auto render=[&]() {
            Check(renderer.Render({0,0,0,1},[&](ID3D12GraphicsCommandList* commands,float) {
                Check(target.Begin(commands,{0,0,0,1}), "Game target begins");
                session.Draw(commands,target.GetWidth(),target.GetHeight());
                Check(target.End(commands), "Game target ends");
            })!=Engine::RenderResult::Failed && renderer.WaitForIdle(), "Game session renders to texture with GPU completion");
        };
        render(); render();
        Check(running->Particle(0)==mote && running->CameraPosition()==camera, "paused rendering cannot advance runtime");
        const auto beforeStep=session.State().Elapsed();
        Check(session.Step() && session.State().CanStep() && session.State().Updates()==2 &&
            std::abs(session.State().Elapsed()-beforeStep-Editor::PlayState::StepSeconds)<1e-12 &&
            running->Particle(0)!=mote, "Step advances runtime exactly one fixed frame while staying paused");
        const auto stepped=running->Particle(0);
        render();
        Check(!session.Update(0.1,true) && running->Particle(0)==stepped && session.State().Updates()==2,
            "drawing and ordinary ticks do not advance after a Step");
        Check(session.Play(renderer,root,layout,error) && session.Runtime()==running && session.Update(0.1,true) &&
            running->Particle(0)!=stepped && session.State().Updates()==3, "Resume continues existing runtime without recreating scene");
        render();
        Check(session.Stop() && session.State().IsEditing() && !session.Runtime() && session.State().Updates()==0,
            "Stop releases runtime after GPU completion and resets state");
        Check(SceneRuntime::SceneLayout::Load(root/"Assets/Scenes/TitleStreet.json").Serialize()==onDisk, "runtime never writes the saved scene");
        auto bad=layout; bad.objects.back().SetModel("Assets/Models/Title/missing-runtime.obj");
        Check(!session.Play(renderer,root,bad,error) && !error.empty() && !session.Runtime() && session.State().IsEditing() &&
            session.State().Elapsed()==0, "failed runtime initialization leaves editor state intact");
        Check(session.Play(renderer,root,{},error) && session.Runtime()->World().Layout().objects.empty() &&
            session.Runtime()->Particle(0)==std::array<float,4>{}, "empty unsaved scene starts with fresh background timing");
        render();
        Check(session.Pause() && session.Stop(), "paused session can stop safely");
    }

    void ValidateEnvironmentMotion(Engine::DirectX12Renderer& renderer)
    {
        const auto root=std::filesystem::absolute("Content");
        auto layout=SceneRuntime::SceneLayout::Load(root/"Assets/Scenes/TitleStreet.json");
        SceneRuntime::SceneEnvironment environment; std::string error;
        Check(environment.Initialize(renderer,root,layout,error),"authored environment initializes");
        const auto original=environment.World().Layout().Serialize();
        const auto home=environment.CameraPosition(); const auto particle=environment.Particle(5);
        environment.Update(0.1,true,true);
        Check(environment.CameraPosition()!=home && environment.Particle(5)!=particle,"authored sway and particles advance");
        const auto paused=environment.CameraPosition(); const auto mote=environment.Particle(5);
        const auto time=environment.MotionSeconds();
        environment.Update(100,false,true);
        Check(!environment.MotionEnabled() && environment.MotionSeconds()==time && environment.CameraPosition()==paused &&
            environment.Particle(5)==mote,"background OFF freezes authored motion and hides particles");
        environment.Update(100,true,false); environment.Update(-1,true,true); environment.Update(NAN,true,true);
        Check(environment.MotionSeconds()==time && environment.CameraPosition()==paused,"inactive and invalid ticks preserve motion");
        for (int frame=0;frame<1500;++frame)
        {
            environment.Update(0.1,true,true);
            const auto camera=environment.CameraPosition();
            float distance=0;
            for (size_t axis=0;axis<3;++axis) distance+=(camera[axis]-home[axis])*(camera[axis]-home[axis]);
            Check(distance<0.012f,"sway stays within authored amplitude over long sessions");
            const auto sample=environment.Particle(static_cast<unsigned int>(frame%24));
            Check(std::all_of(sample.begin(),sample.end(),[](float value) { return std::isfinite(value); }) &&
                sample[3]>=0 && sample[3]<=0.321f,"authored particles stay finite and within opacity range");
        }
        Check(environment.World().Layout().Serialize()==original,"sway and particle phases never modify saved Transform or settings");
        auto* camera=&*std::find_if(layout.objects.begin(),layout.objects.end(),[](const auto& object) { return object.camera.has_value(); });
        camera->cameraSway->enabled=false;
        for (auto& object : layout.objects) if (object.particleEmitter) object.particleEmitter->count=0;
        SceneRuntime::SceneEnvironment stationary;
        Check(stationary.Initialize(renderer,root,layout,error),"disabled sway and zero particle count are valid");
        const auto position=stationary.CameraPosition(); stationary.Update(0.1,true,true);
        Check(stationary.CameraPosition()==position && stationary.Particle(0)==std::array<float,4>{},
            "disabled sway and empty emitter have no hidden fixed title effect");
        Check(renderer.WaitForIdle(),"motion fixture resources safe to release");
    }

    void ValidateSceneView(Engine::DirectX12Renderer& renderer)
    {
        const auto root=std::filesystem::absolute("Content");
        SceneRuntime::SceneLayout layout;
        SceneRuntime::ScenePlacement parent; parent.id="rig"; parent.name="Rig";
        parent.position={3,4,5}; parent.rotation={0,DirectX::XM_PIDIV2,0}; parent.scale={2,3,4};
        SceneRuntime::ScenePlacement camera; camera.id="camera"; camera.name="Camera"; camera.parentId=parent.id;
        camera.position={0,0,2}; camera.camera.emplace(); camera.camera->verticalFov=60;
        camera.directionalLight.emplace(); camera.directionalLight->direction={0,0,1};
        camera.directionalLight->color={0.2f,0.4f,0.6f}; camera.directionalLight->intensity=2;
        camera.sky.emplace(); camera.sky->clouds[1].center={0.12f,0.34f};
        camera.particleEmitter.emplace(); camera.particleEmitter->count=13;
        camera.cameraSway.emplace(); camera.cameraSway->amplitude={0,0,0};
        layout.objects={parent,camera}; layout.settings.mainCamera=camera.id;
        const auto restored=SceneRuntime::SceneLayout::Parse(layout.Serialize());
        Check(restored.settings==layout.settings && restored.objects[1].SameComponents(camera),"Camera and lighting settings round-trip");
        SceneRuntime::SceneWorld world; std::string error;
        Check(world.Initialize(renderer,root,layout,root/"Shaders/Mesh.hlsl",&error),"scene camera fixture initializes");
        Engine::Camera view;
        Check(SceneRuntime::SceneView::Camera(world,16.0f/9.0f,0,view),"main camera resolves");
        Check(std::abs(view.GetPosition()[0]-11)<0.0001f && view.GetPosition()[1]==4 &&
            std::abs(view.GetPosition()[2]-5)<0.0001f,"parent scale and rotation affect camera position once");
        const auto particle0=SceneRuntime::ScenePresentation::Particle(world,camera,0,0);
        const auto particleHalf=SceneRuntime::ScenePresentation::Particle(world,camera,0,6);
        Check(std::abs(particle0[0]-11)<0.0001f && particle0[1]==4 && std::abs(particleHalf[1]-8.5f)<0.0001f &&
            std::abs(particleHalf[3]-camera.particleEmitter->color[3])<0.0001f,
            "particle spawn and travel use full inherited transform and authored opacity");
        const auto light=SceneRuntime::SceneView::Light(world);
        Check(std::abs(light.direction[0]-1)<0.0001f && std::abs(light.direction[2])<0.0001f &&
            light.color==camera.directionalLight->color && light.intensity==2,"light direction inherits rotations and retains authored color");
        const auto reject=[](const SceneRuntime::SceneLayout& candidate) {
            bool failed=false; try { static_cast<void>(candidate.Serialize()); } catch (const std::exception&) { failed=true; }
            Check(failed,"invalid camera/light settings rejected before changing live scene");
        };
        auto invalid=layout; invalid.objects[1].camera->farClip=0.05f; reject(invalid);
        invalid=layout; invalid.settings.mainCamera="missing"; reject(invalid);
        invalid=layout; invalid.objects[1].directionalLight->intensity=NAN; reject(invalid);
        invalid=layout; invalid.objects[1].camera->id="light"; reject(invalid);
        invalid=layout; invalid.objects[1].sky->sunRadius[0]=0; reject(invalid);
        invalid=layout; invalid.objects[1].particleEmitter->count=1025; reject(invalid);
        invalid=layout; invalid.objects[1].cameraSway->period[0]=0; reject(invalid);
        Check(world.SetLocalTransform(camera.id,camera.position,{0.1f,0.2f,0.7f},camera.scale) &&
            SceneRuntime::SceneView::Camera(world,1,0,view),"camera accepts inherited roll and pitch");
        DirectX::XMFLOAT4X4 orientation,viewFrame;
        Check(world.WorldRotation(camera.id,orientation),"camera rotation frame resolves");
        DirectX::XMStoreFloat4x4(&viewFrame,DirectX::XMMatrixInverse(nullptr,view.GetViewMatrix()));
        viewFrame._41=0; viewFrame._42=0; viewFrame._43=0;
        Check(SceneRuntime::SceneTransforms::Matches(orientation,viewFrame),"camera view retains full authored XYZ orientation without parent scale");
        auto disabled=camera; disabled.camera->enabled=false;
        Check(world.SetComponents(camera.id,disabled,root,error) && !SceneRuntime::SceneView::Camera(world,1,0,view),
            "disabled explicit camera does not silently switch to another viewpoint");
        Check(world.SetComponents(camera.id,camera,root,error),"camera restored without replacing object resources");
        Check(world.RemoveObjects({camera.id},error) && world.Layout().settings.mainCamera.empty() &&
            !SceneRuntime::SceneView::Camera(world,1,0,view),"deleting main camera clears dangling reference");
    }

    void ValidateComponents(Engine::DirectX12Renderer& renderer)
    {
        const auto root=std::filesystem::absolute("Content");
        auto layout=SceneRuntime::SceneLayout::Load(root/"Assets/Scenes/ComponentDemo.json");
        SceneRuntime::SceneWorld world;
        std::string error;
        Check(world.Initialize(renderer,root,layout,root/"Shaders/Mesh.hlsl",&error), "component demo initializes");
        const auto json=world.Layout().Serialize();
        Editor::EditState state; state.Select("Spinner");
        Editor::EditHistory history; history.Reset({json,"Spinner",state.SelectedIds()});
        auto settings=layout.objects[1]; settings.rotator->angularVelocity={10,20,30};
        state.RequestComponents(settings,"component/speed");
        auto request=state.TakeRequest();
        Check(request && request->action==Editor::ObjectAction::Components && request->id=="Spinner" &&
            request->components && request->interaction=="component/speed", "Inspector queues typed settings with an interaction identity");
        Check(world.SetComponents(request->id,*request->components,root,error), "Rotator property edit accepted");
        history.Observe({world.Layout().Serialize(),"Spinner",state.SelectedIds()},request->interaction);
        settings.rotator->angularVelocity={20,30,40}; settings.position={999,999,999}; settings.name="Ignored";
        Check(world.SetComponents("Spinner",settings,root,error) && world.Layout().objects[1].position==layout.objects[1].position &&
            world.Layout().objects[1].name==layout.objects[1].name, "component edits cannot overwrite Transform or object metadata");
        history.Observe({world.Layout().Serialize(),"Spinner",state.SelectedIds()},"component/speed"); history.Commit();
        const auto undo=history.Target(false);
        Check(world.ReplaceLayout(SceneRuntime::SceneLayout::Parse(undo.json),root,error), "Undo restores component settings");
        history.Applied(false);
        Check(world.Layout().Serialize()==json && !history.CanUndo() && history.CanRedo(), "one Undo reverses an entire property drag");
        auto invalid=layout.objects[1]; invalid.SetModel("Assets/Models/missing.obj");
        Check(!world.SetComponents("Spinner",invalid,root,error) && world.Layout().Serialize()==json && state.SelectedId()=="Spinner",
            "failed component model loading preserves object settings and selection");
        history.Observe({world.Layout().Serialize(),"Spinner",state.SelectedIds()},{});
        Check(history.CanRedo(), "failed component edit preserves redo history");
        const auto redo=history.Target(true);
        Check(world.ReplaceLayout(SceneRuntime::SceneLayout::Parse(redo.json),root,error) &&
            world.Layout().objects[1].rotator->angularVelocity==std::array<float,3>{20,30,40},
            "Redo reapplies typed Component settings after a failed edit");
        history.Applied(true);
        Check(world.ReplaceLayout(SceneRuntime::SceneLayout::Parse(history.Target(false).json),root,error), "component Undo after Redo");
        history.Applied(false);
        auto child=layout.objects[2]; child.meshRenderer->enabled=false;
        Check(world.SetComponents(child.id,child,root,error) && !world.Layout().objects[2].meshRenderer->enabled,
            "MeshRenderer enable state edits without removing Transform");
        child.meshRenderer.reset();
        Check(world.SetComponents(child.id,child,root,error) && !world.Layout().objects[2].meshRenderer,
            "removing MeshRenderer leaves the object and parent intact");
        Check(world.SetComponents(child.id,layout.objects[2],root,error), "MeshRenderer can be added back");
        auto parent=layout.objects[1]; parent.rotator->enabled=false;
        Check(world.SetComponents(parent.id,parent,root,error) && world.UpdateComponents(1) && world.Layout().objects[1].rotation==parent.rotation,
            "disabled Rotator does not run");
        parent.rotator.reset();
        Check(world.SetComponents(parent.id,parent,root,error) && !world.Layout().objects[1].rotator, "Rotator can be removed");
        Check(world.ReplaceLayout(layout,root,error), "runtime component fixture restored");
        Check(!world.UpdateComponents(NAN) && !world.UpdateComponents(0) && world.Layout().Serialize()==json,
            "invalid runtime ticks leave Transform and Component state intact");
        Editor::PlaySnapshot snapshot(world,state,history,Editor::SceneDocument(root/"Assets/Scenes/ComponentDemo.json"));
        Editor::GameSession session;
        Check(session.Play(renderer,root,layout,error) && session.Update(1,true), "runtime starts and updates Rotator");
        const auto* running=session.Runtime();
        Engine::RenderTexture target;
        Check(target.Resize(renderer,160,90), "component runtime render target");
        Check(renderer.Render({0,0,0,1},[&](ID3D12GraphicsCommandList* commands,float) {
            Check(target.Begin(commands,{0,0,0,1}), "component runtime draw begins");
            session.Draw(commands,target.GetWidth(),target.GetHeight());
            Check(target.End(commands), "component runtime draw ends");
        })!=Engine::RenderResult::Failed && renderer.WaitForIdle(), "component runtime renders inherited poses with GPU completion");
        DirectX::XMFLOAT4X4 childWorld;
        Check(running->World().WorldMatrix("OrbitingChild",childWorld) && std::abs(childWorld._41)<0.001f &&
            std::abs(childWorld._43-7)<0.001f, "Rotator on an empty parent rotates the child once through inherited Transform");
        const auto rotated=running->World().Layout().objects[1].rotation;
        Check(session.Pause() && !session.Update(1,true) && running->World().Layout().objects[1].rotation==rotated,
            "Pause freezes Component updates");
        Check(session.Step() && std::abs(running->World().Layout().objects[1].rotation[1]-rotated[1]-DirectX::XM_PI/120)<0.001f,
            "Step advances Rotator exactly 1/60 second");
        Check(session.Play(renderer,root,layout,error) && session.Update(0.1,true), "Resume continues component runtime");
        Check(renderer.WaitForIdle() && session.Stop() && world.Layout().Serialize()==json,
            "Stop releases runtime without writing simulated transforms into the authoring scene");
        Editor::SceneDocument document(root/"Assets/Scenes/ComponentDemo.json");
        Check(snapshot.Restore(world,root,state,history,document,error) && world.Layout().Serialize()==json && state.SelectedId()=="Spinner",
            "Component scene Stop snapshot preserves settings and selection");
        std::vector<std::string> copies;
        Check(world.DuplicateObjects({"Spinner","OrbitingChild"},{4,0,0},copies,error) &&
            world.Layout().objects[layout.objects.size()].rotator==layout.objects[1].rotator &&
            world.Layout().objects[layout.objects.size()+1].meshRenderer==layout.objects[2].meshRenderer,
            "duplication preserves all component IDs and properties within copied owners");
        const auto saved=std::filesystem::absolute("generated/tests/component-roundtrip.json");
        world.Layout().Save(saved);
        Check(world.ReplaceLayout(SceneRuntime::SceneLayout::Load(saved),root,error), "component scene saves and reopens after duplication");
    }

    void ValidateEmptyObjects(Engine::DirectX12Renderer& renderer)
    {
        const auto root=std::filesystem::absolute("Content");
        SceneRuntime::SceneWorld world;
        std::string error,created;
        Check(world.Initialize(renderer,root,SceneRuntime::SceneLayout{},root/"Shaders/Mesh.hlsl",&error), "empty world initializes");
        SceneRuntime::ScenePlacement parent; parent.position={5,0,0};
        Check(world.AddObject(parent,root,created,error) && !created.empty() && !world.Layout().objects[0].meshRenderer,
            "empty object is created without a model and receives a stable ID");
        const auto parentId=created;
        auto child=parent; child.parentId=parentId; child.position={2,0,0};
        child.SetModel("Assets/Models/Title/Surface/Commercial/building-k.obj");
        Check(world.AddObject(child,root,created,error), "model child can be parented to an empty object");
        DirectX::XMFLOAT4X4 matrix;
        Check(world.WorldMatrix(created,matrix) && std::abs(matrix._41-7)<0.001f,
            "empty parent participates in Transform inheritance");
        Check(world.TranslateObjectsWorld({parentId,created},{3,0,0}) && world.WorldMatrix(created,matrix) &&
            std::abs(matrix._41-10)<0.001f, "empty parents and model children move once per selected branch");
        std::array<std::array<float,3>,8> bounds;
        Check(world.WorldBounds(parentId,bounds), "empty object exposes a focus and gizmo selection marker");
        std::vector<std::string> copies;
        Check(world.DuplicateObjects({parentId,created},{4,0,0},copies,error) && copies.size()==2 &&
            !world.Layout().objects[2].meshRenderer && world.Layout().objects[3].parentId==copies[0],
            "duplicating an empty parent preserves its copied child relationship");
        const auto json=world.Layout().Serialize();
        Check(world.ReplaceLayout(SceneRuntime::SceneLayout::Parse(json),root,error) && world.Layout().Serialize()==json,
            "empty hierarchy survives saving and reopening");
        Check(world.RemoveObjects({parentId},error) && world.WorldMatrix(created,matrix) && std::abs(matrix._41-10)<0.001f,
            "deleting empty parent preserves surviving child world pose");
        Check(!world.PickRay({100,100,100},{0,0,1}), "empty draw objects are skipped safely during ray picking");
        Check(renderer.WaitForIdle(), "empty object resources safe to release");
    }

    void ValidateAssetReload(Engine::DirectX12Renderer& renderer)
    {
        const auto root=std::filesystem::absolute("generated/tests/asset-reload");
        const auto models=root/"Assets/Models/Title";
        std::filesystem::create_directories(models);
        const auto writeModel=[&](int size) {
            std::ofstream file(models/"triangle.obj");
            file << "mtllib triangle.mtl\nv 0 0 0\nv " << size << " 0 0\nv 0 " << size << " 0\nusemtl preview\nf 1 2 3\n";
        };
        { std::ofstream file(models/"triangle.mtl"); file << "newmtl preview\nKd 1 1 1\nmap_Kd preview.png\n"; }
        const auto source=std::filesystem::absolute("Content/Assets/Models/Title/Surface/Textures/plaster.png");
        std::filesystem::copy_file(source,models/"preview.png",std::filesystem::copy_options::overwrite_existing);
        writeModel(1);
        SceneRuntime::ScenePlacement placement;
        placement.id="unsaved"; placement.name="Unsaved name";
        placement.SetModel("Assets/Models/Title/triangle.obj"); placement.position={4,5,6};
        SceneRuntime::SceneLayout layout; layout.objects={placement};
        const auto shader=std::filesystem::absolute("Content/Shaders/Mesh.hlsl");
        SceneRuntime::SceneWorld world;
        std::string error;
        Check(world.Initialize(renderer,root,layout,shader,&error), "asset reload fixture initializes");
        const auto json=world.Layout().Serialize();
        std::array<std::array<float,3>,8> before{},after{};
        Check(world.WorldBounds("unsaved",before), "asset reload captures original model bounds");
        writeModel(2);
        Check(world.ReloadAssets(renderer,root,shader,error) && world.Layout().Serialize()==json &&
            world.WorldBounds("unsaved",after) && before!=after,
            "fresh resource cache reloads changed geometry without replacing unsaved layout");
        before=after;
        { std::ofstream file(models/"preview.png"); file << "invalid image"; }
        Check(!world.ReloadAssets(renderer,root,shader,error) && !error.empty() && world.Layout().Serialize()==json &&
            world.WorldBounds("unsaved",after) && before==after,
            "texture reload failure preserves all live scene resources");
        std::filesystem::copy_file(source,models/"preview.png",std::filesystem::copy_options::overwrite_existing);
        const auto broken=root/"broken.hlsl";
        { std::ofstream file(broken); file << "invalid shader"; }
        Check(!world.ReloadAssets(renderer,root,broken,error) && world.WorldBounds("unsaved",after) && before==after,
            "shader compilation failure preserves the previous model pipeline");
        Check(renderer.WaitForIdle(), "asset reload resources are safe to release");
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
            if (size==TitleSizes[0])
            {
#if defined(_DEBUG) || defined(ENGINE_DEVELOPMENT)
                EditorFontValidation::Run(renderer,TestContentRoot());
#endif
                ValidateAssetReload(renderer);
                ValidateEmptyObjects(renderer);
                EnvironmentValidation::Run(renderer);
                ValidateEnvironmentMotion(renderer);
                UiValidation::Rendering(renderer,TestContentRoot());
                ValidateSceneView(renderer);
                ValidateComponents(renderer);
                ValidatePlaySnapshot(renderer,std::filesystem::absolute("Content"));
                ValidateGameSession(renderer,std::filesystem::absolute("Content"));
                ValidateSceneDocument(renderer,std::filesystem::absolute("Content"));
                ValidateInheritedRendering(renderer,std::filesystem::absolute("Content"));
            }
            {
                SceneRuntime::SceneWorld editorWorld;
                const auto content = std::filesystem::absolute("Content");
                Check(editorWorld.Initialize(renderer, content, content / "Assets/Scenes/TitleStreet.json",
                    content / "Shaders/Mesh.hlsl"), "shared scene loads without App content");
                Engine::RenderTexture sceneTarget;
                Check(sceneTarget.Resize(renderer,96,48), "offscreen scene target");
                Engine::Camera sceneCamera;
                AuthoredViewFixture::SetHome(sceneCamera);
                AuthoredViewFixture::SetProjection(sceneCamera,2.0f);
                Check(renderer.Render({0,0,0,1},[&](ID3D12GraphicsCommandList* commands,float)
                {
                    Check(sceneTarget.Begin(commands,{0,0,0,1}), "offscreen scene begin");
                    editorWorld.Draw(commands,sceneCamera,AuthoredViewFixture::Light());
                    Check(sceneTarget.End(commands), "offscreen scene end");
                })!=Engine::RenderResult::Failed, "shared scene renders to texture");
                Check(renderer.WaitForIdle(), "offscreen scene GPU completion");
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
                nearObject.SetModel("Assets/Models/Title/triangle.obj");
                nearObject.position = { 0, 0, 5 };
                nearObject.scale = { -2, 3, 2 };
                auto farObject = nearObject;
                farObject.id = "pick-far";
                farObject.position[2] = 10;
                pickLayout.objects = { farObject, nearObject };
                const auto pickPath = pickRoot / "scene.json";
                pickLayout.Save(pickPath);
                SceneRuntime::SceneWorld pickingWorld;
                Check(pickingWorld.Initialize(renderer, pickRoot, pickPath, content / "Shaders/Mesh.hlsl"),
                    "picking fixture loaded");
                ValidateGroupMove(renderer,pickingWorld,pickRoot);
                ValidateParentOperations(renderer,pickingWorld,pickRoot);
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
                Check(pickingWorld.SetLocalTransform("pick-near", nearObject.position, { 0, DirectX::XM_PIDIV2, 0 },
                    nearObject.scale) && pickingWorld.PickRay({ -5, 0.6f, 5.4f }, { 1, 0, 0 }).value_or("") == "pick-near",
                    "rotated object uses updated picking transform");
                std::string removalError;
                Check(pickingWorld.RemoveObjects({"pick-near"},removalError) &&
                    pickingWorld.PickRay({ -0.4f, 0.6f, 0 }, { 0, 0, 1 }).value_or("") == "pick-far" &&
                    !pickingWorld.WorldBounds("pick-near", corners), "deleted object cannot be selected or outlined");
                Check(renderer.WaitForIdle(), "picking resources GPU completion");
                const auto original = editorWorld.Layout().objects.front();
                auto moved = original.position;
                moved[2] += 2.0f;
                const std::array<float, 3> rotated{ 0.1f, 0.2f, 0.3f };
                const std::array<float, 3> scaled{ 120, 4, 160 };
                Check(editorWorld.SetLocalTransform(original.id, moved, rotated, scaled), "live transform edit accepted");
                const auto& changed = editorWorld.Layout().objects.front();
                Check(changed.position == moved && changed.rotation == rotated && changed.scale == scaled &&
                    changed.Model()==original.Model(), "live edits update layout while keeping model reference");
                Check(!editorWorld.SetLocalTransform(original.id, original.position, original.rotation, { 0, 4, 4 }),
                    "zero scale edit rejected");
                auto invalidPosition = original.position;
                invalidPosition[0] = NAN;
                Check(!editorWorld.SetLocalTransform(original.id, invalidPosition, original.rotation, original.scale),
                    "nonfinite position edit rejected");
                Check(!editorWorld.SetLocalTransform("missing-object", original.position, original.rotation, original.scale),
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
                    content / "Shaders/Mesh.hlsl", &startupError) && !startupError.empty(),
                    "startup load failure provides a diagnostic");
                Check(startupFailure.Reload(content, content / "Assets/Scenes/TitleStreet.json", startupError),
                    "failed initial load can be recovered by reload");
                Check(renderer.WaitForIdle(), "startup recovery GPU completion");
                std::string reloadError;
                Check(!editorWorld.Reload(content, content / "Assets/Scenes/missing.json", reloadError) &&
                    !reloadError.empty() && editorWorld.Layout().objects.front().position == moved,
                    "failed reload preserves live edits and provides error");
                auto invalidLayout = editorWorld.Layout();
                invalidLayout.objects.back().SetModel("Assets/Models/Title/Roads/missing.obj");
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
                added.SetModel("Assets/Models/Title/Commercial/detail-awning.obj");
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
                added.SetModel("Assets/Models/Title/Roads/missing.obj");
                Check(!editorWorld.AddObject(added, content, rejectedId, operationError) &&
                    editorWorld.Layout().objects.size() == count + 2, "failed addition keeps all current objects");
                Check(!editorWorld.DuplicateObject(addedId, { NAN, 0, 0 }, rejectedId, operationError) &&
                    editorWorld.Layout().objects.size() == count + 2, "invalid duplicate keeps current scene");
                Check(!editorWorld.RemoveObjects({"missing-id"},operationError), "unknown delete does not change scene");
                const auto beforeDelete=editorWorld.Layout();
                Check(editorWorld.RemoveObjects({addedId},operationError) && editorWorld.Layout().objects.size() == count + 1 &&
                    editorWorld.Layout().objects.back().id == duplicateId, "delete removes only selected object");
                const auto afterDelete=editorWorld.Layout();
                Check(editorWorld.ReplaceLayout(beforeDelete, content, operationError) &&
                    editorWorld.Layout().Serialize()==beforeDelete.Serialize(), "undo restores deleted object ID and ordering");
                auto brokenSnapshot=beforeDelete;
                brokenSnapshot.objects.back().SetModel("Assets/Models/Title/Roads/missing.obj");
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
                Editor::EditState editState;
                editState.Select(duplicateId);
                editState.Request({Editor::ObjectAction::Duplicate, editState.SelectedId(), {}, {}});
                const auto request = editState.TakeRequest();
                Check(request && request->id == duplicateId && !editState.TakeRequest(),
                    "shared edit request consumed exactly once");
                Editor::EditHistory workflow;
                const auto initialJson=editorWorld.Layout().Serialize();
                workflow.Reset({initialJson,editState.SelectedId()});
                std::string workflowId;
                Check(editorWorld.DuplicateObject(duplicateId,{4,0,0},workflowId,operationError), "workflow duplicate");
                editState.ObjectChanged(workflowId);
                const auto duplicatedJson=editorWorld.Layout().Serialize();
                workflow.Observe({duplicatedJson,workflowId},{});
                auto edited=editorWorld.Layout().objects.back();
                for (int frame=0;frame<3;++frame)
                {
                    edited.position[0]+=1;
                    edited.rotation[1]+=0.1f;
                    edited.scale[0]=-4;
                    Check(editState.SetLocalTransform(editorWorld,workflowId,edited.position,edited.rotation,edited.scale), "workflow transform");
                    workflow.Observe({editorWorld.Layout().Serialize(),editState.SelectedId()},"drag");
                }
                const auto transformedJson=editorWorld.Layout().Serialize();
                Check(!editState.SetLocalTransform(editorWorld,workflowId,edited.position,edited.rotation,{0,1,1}) &&
                    editState.InvalidTransform() && editorWorld.Layout().Serialize()==transformedJson,
                    "shared edit rejects invalid transform without changing scene");
                editState.Select(workflowId);
                Check(!editState.InvalidTransform() && editState.HasChanges(),
                    "selection clears transform error and preserves unsaved state");
                workflow.Observe({transformedJson,workflowId},{});
                const auto applyHistory=[&](bool redo)
                {
                    Check(redo ? workflow.CanRedo() : workflow.CanUndo(), "workflow history entry exists");
                    const auto target=workflow.Target(redo);
                    Check(editorWorld.ReplaceLayout(SceneRuntime::SceneLayout::Parse(target.json),content,operationError),
                        "workflow history restore succeeds");
                    workflow.Applied(redo);
                    Check(editorWorld.Layout().Serialize()==target.json,"workflow restores complete scene exactly");
                    editState.Select(target.selection);
                    editState.SetChanged(workflow.Dirty(target.json));
                    return editState.SelectedId();
                };
                Check(applyHistory(false)==workflowId && editorWorld.Layout().Serialize()==duplicatedJson,
                    "one undo reverses the whole continuous transform");
                Check(applyHistory(false)==duplicateId && editorWorld.Layout().Serialize()==initialJson,
                    "second undo reverses duplicate and restores selection");
                applyHistory(true);
                applyHistory(true);
                Check(editorWorld.Layout().Serialize()==transformedJson,"redo restores mirrored transformed duplicate");
                editorWorld.Layout().Save(editedPath);
                workflow.Saved(transformedJson);
                editState.MarkSaved();
                Check(!editState.HasChanges(), "shared edit state marked saved");
                Check(editorWorld.RemoveObjects({workflowId},operationError),"workflow delete");
                editState.ObjectChanged("");
                workflow.Observe({editorWorld.Layout().Serialize(),editState.SelectedId()},{});
                Check(workflow.Dirty(editorWorld.Layout().Serialize()),"deleting saved object marks scene dirty");
                Check(applyHistory(false)==workflowId && !workflow.Dirty(editorWorld.Layout().Serialize()),
                    "undo deletion restores selected object and saved state");
                Check(editorWorld.Reload(content,editedPath,operationError) &&
                    editorWorld.Layout().Serialize()==transformedJson,"workflow save and reload preserves final transforms");
                Editor::EditHistory inspectorHistory;
                inspectorHistory.Reset({transformedJson,workflowId});
                const std::string renamedName="新しい建物##display";
                Check(editState.Rename(editorWorld,workflowId,renamedName), "Inspector rename accepted");
                const auto renamedJson=editorWorld.Layout().Serialize();
                inspectorHistory.Observe({renamedJson,workflowId},{});
                Check(editorWorld.Layout().objects.back().id==workflowId &&
                    editorWorld.Layout().objects.back().name==renamedName, "rename preserves object ID and Unicode name");
                Check(!editState.Rename(editorWorld,workflowId," \t") && !editState.Rename(editorWorld,"missing-id","name") &&
                    editorWorld.Layout().Serialize()==renamedJson, "invalid rename preserves scene");
                Check(editState.ResetTransform(editorWorld,workflowId), "Inspector transform reset accepted");
                const auto& reset=editorWorld.Layout().objects.back();
                Check(reset.position==std::array<float,3>{0,0,0} && reset.rotation==std::array<float,3>{0,0,0} &&
                    reset.scale==std::array<float,3>{1,1,1} && reset.name==renamedName, "reset restores identity and keeps name");
                const auto resetJson=editorWorld.Layout().Serialize();
                inspectorHistory.Observe({resetJson,workflowId},{});
                const auto restoreInspector=[&](bool redo)
                {
                    const auto target=inspectorHistory.Target(redo);
                    Check(editorWorld.ReplaceLayout(SceneRuntime::SceneLayout::Parse(target.json),content,operationError),
                        "Inspector history restoration");
                    inspectorHistory.Applied(redo);
                    editState.Select(target.selection);
                };
                restoreInspector(false);
                Check(editorWorld.Layout().Serialize()==renamedJson, "one undo restores pre-reset mirrored transform");
                restoreInspector(false);
                Check(editorWorld.Layout().Serialize()==transformedJson, "next undo restores original name");
                restoreInspector(true);
                restoreInspector(true);
                Check(editorWorld.Layout().Serialize()==resetJson && editState.SelectedId()==workflowId,
                    "redo restores rename and reset with selection");
                editorWorld.Layout().Save(editedPath);
                Check(editorWorld.Reload(content,editedPath,operationError) && editorWorld.Layout().Serialize()==resetJson,
                    "Inspector name and reset survive save and reload");

                App::TitleScene title(TestContentRoot());
                Check(title.Initialize(renderer), "title assets and sprite pipeline");
                Check(title.Draw(renderer) != Engine::RenderResult::Failed, "title rendering");
                Check(renderer.WaitForIdle(), "title GPU completion");
                SceneRuntime::SceneEnvironment environment;
                Check(environment.Initialize(renderer,TestContentRoot(),TestContentRoot()/"Assets/Scenes/TitleStreet.json",operationError), "ambient environment assets");
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
    Check(std::count_if(layout.objects.begin(),layout.objects.end(),[](const auto& p){return p.meshRenderer.has_value();}) == 123, "all existing street mesh placements migrated");
    Check(layout.objects.front().id == "ground" && layout.objects.front().position[2] == 60.0f,
        "ground placement preserved");
    const std::string entry = R"({"id":"test","name":"Test","model":"Assets/Models/Title/Roads/ground.obj","position":[1,2,3],"rotation":[0,1,0],"scale":[4,4,4]})";
    const auto parse = [](const std::string& objects) {
        return SceneRuntime::SceneLayout::Parse("{\"version\":2,\"objects\":[" + objects + "]}");
    };
    Check(parse(entry).objects[0].rotation[1] == 1.0f, "full transform read from JSON");
    const auto reject = [](const std::string& json) {
        bool rejected = false;
        try { static_cast<void>(SceneRuntime::SceneLayout::Parse(json)); }
        catch (const std::exception&) { rejected = true; }
        Check(rejected, "invalid layout rejected");
    };
    reject("{invalid}");
    reject("{\"version\":1,\"objects\":[]}");
    reject("{\"version\":2,\"transformSpace\":\"world\",\"objects\":[]}");
    reject("{\"version\":2,\"transformSpace\":\"local\",\"objects\":[]}");
    const auto empty=SceneRuntime::SceneLayout::Parse("{\"version\":2,\"objects\":[]}");
    Check(empty.Serialize().find("transformSpace")==std::string::npos, "v2 stores only parent-relative transforms");
    reject("{\"version\":2,\"objects\":[" + entry + "," + entry + "]}");
    auto invalid = entry;
    invalid.replace(invalid.find("[4,4,4]"), 7, "[0,4,4]");
    reject("{\"version\":2,\"objects\":[" + invalid + "]}");
    invalid = entry;
    invalid.replace(invalid.find("Roads/ground.obj"), 15, "../ground.obj");
    reject("{\"version\":2,\"objects\":[" + invalid + "]}");
}

void ValidateComponentSchema()
{
    SceneRuntime::ScenePlacement empty;
    empty.id="empty"; empty.name="Empty";
    SceneRuntime::SceneLayout layout; layout.objects={empty};
    const auto json=layout.Serialize();
    auto restored=SceneRuntime::SceneLayout::Parse(json);
    Check(json.find("\"version\": 4")!=std::string::npos && !restored.objects[0].meshRenderer &&
        !restored.objects[0].rotator, "version 4 supports Transform-only objects");
    restored.settings.background={0.1f,0.2f,0.3f,0.4f};
    Check(SceneRuntime::SceneLayout::Parse(restored.Serialize()).settings==restored.settings,
        "scene background round-trips without altering objects");
    restored.settings.fog={true,{0.1f,0.3f,0.5f},5,40,0.6f};
    Check(SceneRuntime::SceneLayout::Parse(restored.Serialize()).settings==restored.settings,"fog configuration round-trips");
    auto invalidFog=restored; invalidFog.settings.fog.end=invalidFog.settings.fog.start;
    bool rejectedFog=false;
    try { static_cast<void>(invalidFog.Serialize()); } catch (const std::exception&) { rejectedFog=true; }
    Check(rejectedFog,"zero fog range is rejected");
    auto badBackground=restored; badBackground.settings.background[0]=2;
    bool rejectedBackground=false;
    try { static_cast<void>(badBackground.Serialize()); } catch (const std::exception&) { rejectedBackground=true; }
    Check(rejectedBackground,"out-of-range scene background is rejected");
    restored.objects[0].SetModel("Assets/Models/Title/Surface/Commercial/building-k.obj");
    restored.objects[0].meshRenderer->id="custom-mesh"; restored.objects[0].meshRenderer->enabled=false;
    restored.objects[0].rotator=SceneRuntime::RotatorComponent{"spin",false,{10,-20,30}};
    const auto copy=SceneRuntime::SceneLayout::Parse(restored.Serialize());
    Check(copy.objects[0].meshRenderer==restored.objects[0].meshRenderer && copy.objects[0].rotator==restored.objects[0].rotator,
        "component IDs enable states model and typed velocity round-trip");
    const auto reject=[](const SceneRuntime::SceneLayout& invalid) {
        bool failed=false; try { static_cast<void>(invalid.Serialize()); } catch (const std::exception&) { failed=true; }
        Check(failed, "invalid component settings cannot be saved");
    };
    auto upper=restored; upper.objects[0].SetModel("Assets/Models/Custom.OBJ");
    Check(SceneRuntime::SceneLayout::Parse(upper.Serialize()).objects[0].Model()==upper.objects[0].Model(),
        "component model references accept Project-recognized uppercase OBJ extensions outside Title");
    auto invalid=restored; invalid.objects[0].rotator->id="custom-mesh"; reject(invalid);
    invalid=restored; invalid.objects[0].rotator->id="transform"; reject(invalid);
    invalid=restored; invalid.objects[0].rotator->angularVelocity[0]=NAN; reject(invalid);
    invalid=restored; invalid.objects[0].rotator->angularVelocity[0]=100001; reject(invalid);
    invalid=restored; invalid.objects[0].meshRenderer->model="../outside.obj"; reject(invalid);
    const std::string entry=R"({"id":"old","name":"Old","model":"Assets/Models/Title/Roads/ground.obj","position":[1,2,3],"rotation":[0,0,0],"scale":[1,1,1]})";
    const auto migrated=SceneRuntime::SceneLayout::Parse("{\"version\":2,\"objects\":["+entry+"]}");
    Check(migrated.objects[0].meshRenderer && migrated.objects[0].meshRenderer->id=="mesh" &&
        migrated.objects[0].meshRenderer->enabled && migrated.objects[0].position==std::array<float,3>{1,2,3} &&
        migrated.Serialize().find("\"components\"")!=std::string::npos, "version 2 migration preserves local poses and adds MeshRenderer once");
    const std::string prefix=R"({"version":3,"objects":[{"id":"bad","name":"Bad","position":[0,0,0],"rotation":[0,0,0],"scale":[1,1,1],"components":[)";
    for (const auto* component : {R"({"id":"unknown","type":"Missing","enabled":true})",
        R"({"id":"spin","type":"Rotator","enabled":"true","angularVelocity":[0,90,0]})"})
    {
        bool failed=false; try { static_cast<void>(SceneRuntime::SceneLayout::Parse(prefix+component+"]}] }")); }
        catch (const std::exception&) { failed=true; }
        Check(failed, "unknown component types and wrong property types are rejected instead of discarded");
    }
}

void ValidateAssetChangeBatching()
{
    Editor::AssetChanges changes;
    Editor::AssetChanges::Files files;
    const auto time=std::filesystem::file_time_type::clock::now();
    files.emplace("mesh.obj",Editor::AssetChanges::Stamp{time,1});
    changes.Observe(files);
    Check(!changes.Pending(), "initial asset inventory does not reload");
    files.at("mesh.obj").bytes=2; changes.Observe(files);
    Check(changes.Pending() && !changes.TakeReady(true), "asset changes wait for a stable inventory");
    changes.Observe(files);
    Check(!changes.TakeReady(false) && changes.Pending(), "Play and Pause retain queued reloads");
    files.emplace("material.mtl",Editor::AssetChanges::Stamp{time,2}); changes.Observe(files);
    Check(!changes.TakeReady(true), "additional dependent changes restart batching");
    changes.Observe(files);
    Check(changes.TakeReady(true) && !changes.TakeReady(true), "stable batch applies once after returning to editing");
    const auto root=std::filesystem::absolute("generated/tests/asset-watch");
    std::filesystem::create_directories(root/"Assets/Scenes");
    std::filesystem::create_directories(root/"Shaders");
    for (const auto* name : {"scene.json","scene.JSON","scene.json.tmp"})
    { std::ofstream file(root/"Assets/Scenes"/name); file << "{}"; }
    { std::ofstream file(root/"Assets/shape.MTL"); file << "newmtl test"; }
    { std::ofstream file(root/"Shaders/test.HLSL"); file << "source"; }
    const auto captured=Editor::AssetChanges::Capture(root);
    Check(!captured.contains(root/"Assets/Scenes/scene.JSON") && !captured.contains(root/"Assets/Scenes/scene.json.tmp") &&
        captured.contains(root/"Assets/shape.MTL") && captured.contains(root/"Shaders/test.HLSL"),
        "watching includes material and shader dependencies but ignores scenes and temporary saves case-insensitively");
    std::string error;
    Check(Editor::ValidateProjectShaders(std::filesystem::absolute("Content"),error), "project shader entry points compile");
}

void ValidateEditorAcceptanceScene()
{
    const auto layout = SceneRuntime::SceneLayout::Load("Content/Assets/Scenes/EditorAcceptance.json");
    const auto restored = SceneRuntime::SceneLayout::Parse(layout.Serialize());
    std::vector<DirectX::XMFLOAT4X4> worlds;
    std::string error;
    Check(SceneRuntime::SceneTransforms::Resolve(restored, worlds, error),
        "manual acceptance scene resolves after serialization");
    for (const auto& object : restored.objects)
        if (object.meshRenderer) Check(std::filesystem::is_regular_file(std::filesystem::path("Content") / object.Model()),
            "manual acceptance scene references an existing model");
    const auto matrix = [&](const std::string& id) -> const DirectX::XMFLOAT4X4& {
        const auto found = std::find_if(restored.objects.begin(), restored.objects.end(),
            [&](const auto& object) { return object.id == id; });
        Check(found != restored.objects.end(), "manual acceptance target exists");
        return worlds.at(static_cast<size_t>(found - restored.objects.begin()));
    };
    Check(std::abs(matrix("Child")._41 + 2.0f) < 0.001f &&
        std::abs(matrix("Grandchild")._43 - 10.0f) < 0.001f,
        "manual hierarchy baseline has the documented world positions");
    Check(DirectX::XMVectorGetX(DirectX::XMMatrixDeterminant(
        DirectX::XMLoadFloat4x4(&matrix("Mirror")))) < 0.0f,
        "manual mirror baseline retains negative determinant");
    SceneRuntime::ScenePlacement reference;
    SceneRuntime::ScenePlacement decomposed;
    Check(!SceneRuntime::SceneTransforms::ReadTransform(matrix("ShearChild"), reference, decomposed),
        "manual shear baseline cannot be reparented to root without changing its shape");
}

void ValidateSceneTransforms()
{
    using SceneRuntime::SceneTransforms;
    SceneRuntime::ScenePlacement parent;
    parent.id="parent";
    parent.position={10,20,30};
    parent.rotation={0,DirectX::XM_PIDIV2,0};
    parent.scale={2,3,4};
    auto child=parent;
    child.id="child"; child.parentId=parent.id;
    child.position={1,2,3}; child.rotation={}; child.scale={1,1,1};
    auto grandchild=child;
    grandchild.id="grandchild"; grandchild.parentId=child.id; grandchild.position={0,1,0};
    SceneRuntime::SceneLayout layout;
    layout.objects={grandchild,child,parent};
    std::vector<DirectX::XMFLOAT4X4> worlds;
    std::string error;
    Check(SceneTransforms::Resolve(layout,worlds,error) && worlds.size()==3,
        "local transforms resolve parent-first independent of storage order");
    Check(std::abs(worlds[1]._41-22)<0.001f && std::abs(worlds[1]._42-26)<0.001f && std::abs(worlds[1]._43-28)<0.001f &&
        std::abs(worlds[0]._42-29)<0.001f, "parent scale, rotation and translation affect descendants in row-vector order");
    DirectX::XMFLOAT4X4 local;
    Check(SceneTransforms::WorldToLocal(worlds[1],worlds[2],local) &&
        std::abs(local._41-1)<0.001f && std::abs(local._42-2)<0.001f && std::abs(local._43-3)<0.001f,
        "world to local conversion inverts transformed parent");
    layout.objects[1].rotation[2]=0.37f;
    Check(SceneTransforms::Resolve(layout,worlds,error), "nonuniform parent with rotated child resolves full affine matrix");
    const float dot=worlds[1]._11*worlds[1]._21+worlds[1]._12*worlds[1]._22+worlds[1]._13*worlds[1]._23;
    Check(std::abs(dot)>0.1f, "inherited shear is retained instead of approximated by a TRS");
    Check(SceneTransforms::WorldToLocal(worlds[1],worlds[2],local), "sheared world converts to local matrix");
    DirectX::XMFLOAT4X4 recomposed;
    DirectX::XMStoreFloat4x4(&recomposed,DirectX::XMLoadFloat4x4(&local)*DirectX::XMLoadFloat4x4(&worlds[2]));
    Check(SceneRuntime::SceneTransforms::Matches(recomposed,worlds[1]), "world local world roundtrip preserves shear");
    layout.objects[2].scale[0]=-2;
    Check(SceneTransforms::Resolve(layout,worlds,error) && SceneTransforms::IsUsable(worlds[0]),
        "mirrored parent transforms remain usable");
    const auto unchanged=worlds;
    const auto rejects=[&](const SceneRuntime::SceneLayout& invalid)
    {
        Check(!SceneTransforms::Resolve(invalid,worlds,error) && !error.empty() &&
            worlds.size()==unchanged.size() && SceneRuntime::SceneTransforms::Matches(worlds[0],unchanged[0]),
            "invalid transform graph preserves output matrices");
    };
    auto invalid=layout; invalid.objects[2].parentId=invalid.objects[0].id; rejects(invalid);
    invalid=layout; invalid.objects[1].parentId="missing"; rejects(invalid);
    invalid=layout; invalid.objects[1].parentId=invalid.objects[1].id; rejects(invalid);
    invalid=layout; invalid.objects[1].id=invalid.objects[0].id; rejects(invalid);
    invalid=layout; invalid.objects[1].scale[0]=0; rejects(invalid);
    invalid=layout; invalid.objects[1].position[0]=NAN; rejects(invalid);
    auto singular=worlds[2]; singular._11=singular._12=singular._13=0;
    const auto previous=local;
    Check(!SceneTransforms::WorldToLocal(worlds[1],singular,local) && SceneRuntime::SceneTransforms::Matches(local,previous),
        "singular parent is rejected without changing local output");
    auto perspective=worlds[1]; perspective._14=0.1f;
    Check(!SceneTransforms::IsUsable(perspective), "perspective matrices are excluded from affine transform calculations");
    SceneRuntime::SceneLayout deep;
    for (int index=0;index<2000;++index)
    {
        SceneRuntime::ScenePlacement node;
        node.id="transform-"+std::to_string(index);
        node.position={1,0,0};
        if (index) node.parentId="transform-"+std::to_string(index-1);
        deep.objects.push_back(std::move(node));
    }
    std::reverse(deep.objects.begin(),deep.objects.end());
    Check(SceneTransforms::Resolve(deep,worlds,error) && worlds.front()._41==2000,
        "deep local hierarchy resolves iteratively without stack overflow");
    Check(SceneTransforms::Resolve({},worlds,error) && worlds.empty() && error.empty(),
        "empty transform graph succeeds and clears previous result");
}

void ValidateHierarchyRows()
{
    SceneRuntime::ScenePlacement root;
    root.id="root";
    auto child=root; child.id="child"; child.parentId=root.id;
    auto grandchild=root; grandchild.id="grandchild"; grandchild.parentId=child.id;
    auto other=root; other.id="other";
    SceneRuntime::SceneLayout layout;
    layout.objects={grandchild,child,root,other};
    const auto rows=Editor::BuildHierarchyRows(layout,{});
    Check(rows.size()==4 && rows[0].index==2 && rows[0].depth==0 && rows[0].children &&
        rows[1].index==1 && rows[1].depth==1 && rows[1].children && rows[2].index==0 && rows[2].depth==2 &&
        !rows[2].children && rows[3].index==3 && rows[3].depth==0, "Hierarchy preorder follows parents independently of storage order");
    const auto collapsed=Editor::BuildHierarchyRows(layout,{"child"});
    Check(collapsed.size()==3 && collapsed[0].index==2 && collapsed[1].index==1 && collapsed[2].index==3,
        "collapsed parent hides descendants but preserves other roots");
    Check(Editor::BuildHierarchyRows(layout,{"root"}).size()==2 && Editor::BuildHierarchyRows({},{}).empty(),
        "root collapse and empty Hierarchy remain valid");
}

void ValidateParentData()
{
    auto rootSource=SceneRuntime::SceneLayout::Load("Content/Assets/Scenes/TitleStreet.json");
    for (auto& object : rootSource.objects) object.parentId.clear();
    const auto rootLayout=SceneRuntime::SceneLayout::Parse(rootSource.Serialize());
    Check(std::all_of(rootLayout.objects.begin(),rootLayout.objects.end(),[](const auto& object) { return object.parentId.empty(); }),
        "objects without parent IDs are roots");
    auto root=rootLayout.objects.front();
    root.id="親";
    auto child=root; child.id="child"; child.parentId=root.id;
    auto grandchild=root; grandchild.id="grandchild"; grandchild.parentId=child.id;
    SceneRuntime::SceneLayout layout;
    layout.objects={grandchild,child,root};
    const auto json=layout.Serialize();
    const auto restored=SceneRuntime::SceneLayout::Parse(json);
    Check(restored.objects[0].parentId=="child" && restored.objects[1].parentId=="親" &&
        restored.objects[2].parentId.empty(), "parent IDs support UTF-8, forward references and roots in any object order");
    const auto directory=std::filesystem::absolute("generated/tests/parent-data");
    std::filesystem::create_directories(directory);
    const auto path=directory/"scene.json";
    layout.Save(path);
    Check(SceneRuntime::SceneLayout::Load(path).Serialize()==json, "parent relationships round trip through scene file");
    const auto rejects=[](const SceneRuntime::SceneLayout& invalid)
    {
        bool rejected=false;
        try { static_cast<void>(invalid.Serialize()); } catch (const std::exception&) { rejected=true; }
        Check(rejected, "invalid parent graph rejected on serialization");
    };
    auto invalid=layout;
    invalid.objects[0].parentId="missing"; rejects(invalid);
    invalid=layout; invalid.objects[0].parentId=invalid.objects[0].id; rejects(invalid);
    invalid=layout; invalid.objects[1].parentId=invalid.objects[0].id; rejects(invalid);
    invalid=layout; invalid.objects[2].parentId=invalid.objects[0].id; rejects(invalid);
    bool failed=false;
    try { invalid.Save(path); } catch (const std::exception&) { failed=true; }
    Check(failed && SceneRuntime::SceneLayout::Load(path).Serialize()==json, "invalid parent graph cannot overwrite existing scene");
    const auto parent=json.find("\"parent\":");
    const auto comma=json.find(',',parent);
    for (const auto* value : {"42","null","true"})
    {
        auto typed=json;
        typed.replace(parent,comma-parent,std::string("\"parent\":")+value);
        failed=false;
        try { static_cast<void>(SceneRuntime::SceneLayout::Parse(typed)); } catch (const std::exception&) { failed=true; }
        Check(failed, "present parent field must be a string");
    }
    SceneRuntime::SceneLayout deep;
    for (int index=0;index<2000;++index)
    {
        auto node=root;
        node.id="node-"+std::to_string(index);
        if (index) node.parentId="node-"+std::to_string(index-1);
        deep.objects.push_back(std::move(node));
    }
    std::reverse(deep.objects.begin(),deep.objects.end());
    const auto deepRows=Editor::BuildHierarchyRows(deep,{});
    Check(deepRows.size()==2000 && deepRows.back().depth==1999, "deep Hierarchy rows build without recursive traversal");
    Check(SceneRuntime::SceneLayout::Parse(deep.Serialize()).objects.size()==2000, "deep parent graph validates without recursive traversal");
    deep.objects.back().parentId=deep.objects.front().id;
    rejects(deep);
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
        restored.objects.back().Model()==layout.objects.back().Model(), "layout UTF8 and transforms round-trip");
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

void ValidateTransformDecomposition()
{
    SceneRuntime::ScenePlacement placement;
    placement.position={8,-2,12};
    for (const auto& scale : {std::array<float,3>{2,3,4}, std::array<float,3>{-2,3,4}, std::array<float,3>{-2,-3,4}})
    for (float y : {0.3f, 2.1f, DirectX::XM_PIDIV2, -DirectX::XM_PIDIV2})
    {
        placement.scale=scale;
        placement.rotation={0.7f,y,-0.4f};
        const auto matrix=ComposeTransform(placement);
        auto result=placement;
        Check(SceneRuntime::SceneTransforms::ReadTransform(matrix,placement,result), "gizmo matrix round trip including mirrored and gimbal poses");
        for (int i=0;i<3;++i)
            Check(std::abs(result.scale[i]-scale[i])<0.001f && std::abs(result.rotation[i]-placement.rotation[i])<0.001f,
                "gizmo conversion preserves mirror signs and continuous Euler angles");
    }
    placement.rotation={0,0,0};
    placement.scale={1,1,1};
    auto invalid=ComposeTransform(placement);
    invalid._12=0.5f;
    auto output=placement;
    Check(!SceneRuntime::SceneTransforms::ReadTransform(invalid,placement,output) && output.rotation==placement.rotation,
        "sheared gizmo matrix is rejected without changing placement");
    invalid=ComposeTransform(placement);
    invalid._11=0;
    Check(!SceneRuntime::SceneTransforms::ReadTransform(invalid,placement,output), "zero scale is rejected");
    invalid._11=NAN;
    Check(!SceneRuntime::SceneTransforms::ReadTransform(invalid,placement,output), "nonfinite transform is rejected");
    Check(!SceneRuntime::SceneTransforms::Matches(invalid,invalid), "nonfinite matrices never compare as matching");
}

void ValidateSceneViewport()
{
    const Editor::SceneViewport viewport{120, 80, 800, 400};
    Check(viewport.Valid() && viewport.Aspect()==2, "offset scene viewport aspect");
    Check(viewport.ToNdc(520,280)==std::array<float,2>{0,0}, "scene center maps to NDC origin");
    Check(viewport.ToNdc(120,80)==std::array<float,2>{-1,1}, "scene top left maps to NDC corner");
    Check(!viewport.ToNdc(119,280) && !viewport.ToNdc(520,79) &&
        !viewport.ToNdc(920,280) && !viewport.ToNdc(520,480), "scene excludes outside and right/bottom boundary");
    Check(viewport.ToScreen(0,0)==std::array<float,2>{520,280} &&
        viewport.ToScreen(1,-1)==std::array<float,2>{920,480}, "selection overlay includes viewport offset");
    for (const auto& invalid : std::array<Editor::SceneViewport,5>{
        Editor::SceneViewport{}, {0,0,800,0}, {0,0,-1,400}, {NAN,0,800,400}, {0,0,INFINITY,400}})
        Check(!invalid.Valid() && !invalid.ToNdc(0,0), "invalid viewport disables picking");
    const Editor::SceneViewport resized{32,64,400,800};
    Check(resized.Aspect()==0.5f && resized.ToNdc(232,464)==std::array<float,2>{0,0},
        "resized portrait scene keeps its center and aspect");
}

void ValidateMultiSelection()
{
    Editor::EditState state;
    state.Select("a");
    state.Select("b",true);
    state.Select("c",true);
    Check(state.SelectedIds()==std::vector<std::string>{"a","b","c"} && state.SelectedId()=="c" &&
        !state.SingleSelection() && !state.HasChanges(), "Ctrl selection adds IDs and primary without changing scene");
    state.Select("c",true);
    Check(state.SelectedId()=="b" && !state.IsSelected("c"), "Ctrl deselection falls back to last remaining ID");
    state.Select("",true);
    Check(state.SelectedIds().size()==2, "modified empty Scene click preserves selection");
    state.Select("a");
    Check(state.SingleSelection() && state.SelectedId()=="a", "normal click replaces multi selection");
    const std::vector<std::string> visible{"a","c","e","g"};
    state.SelectRange(visible,"a","e",false);
    Check(state.SelectedIds()==std::vector<std::string>{"a","c","e"}, "Shift range uses only filtered visible objects");
    state.SelectRange(visible,"g","c",true);
    Check(state.SelectedIds()==std::vector<std::string>{"a","e","g","c"} && state.SelectedId()=="c",
        "Ctrl Shift extends reversed range without duplicates and makes clicked object primary");
    state.SelectRange(visible,"hidden","g",false);
    Check(state.SingleSelection() && state.SelectedId()=="g", "missing range anchor falls back to single selection");
    state.RestoreSelection({"a","a","","c"},"c");
    Check(state.SelectedIds()==std::vector<std::string>{"a","c"}, "selection restoration removes duplicates and empty IDs");
    state.RestoreSelection(state.SelectedIds(),state.SelectedId());
    Check(state.SelectedIds()==std::vector<std::string>{"a","c"}, "restoring current selection is alias-safe");
    Editor::EditHistory history;
    history.Reset({"initial","c",state.SelectedIds()});
    state.Select("e",true);
    history.Observe({"initial",state.SelectedId(),state.SelectedIds()},{});
    Check(!history.CanUndo() && !history.Dirty("initial"), "selection-only change creates no Undo entry or dirty flag");
    history.Observe({"changed","a",{"a"}},{});
    const auto undo=history.Target(false);
    state.RestoreSelection(undo.selections,undo.selection);
    Check(state.SelectedIds()==std::vector<std::string>{"a","c","e"} && state.SelectedId()=="e",
        "Undo snapshot restores full multi selection and primary");
    state.SetChanged(true);
    state.Select("");
    Check(state.SelectedIds().empty() && state.SelectedId().empty() && state.HasChanges(),
        "empty Scene click clears selection while preserving dirty state");
    state.Select("a");
    state.Reloaded();
    Check(state.SelectedIds().empty() && !state.HasChanges(), "scene reload clears multi selection and dirty state");
}

void ValidateSaveAs()
{
    const auto root=std::filesystem::absolute("generated/tests/scene-save-as");
    std::filesystem::create_directories(root/"Assets/Scenes");
    const auto original=root/"Assets/Scenes/original.json";
    const auto existing=root/"Assets/Scenes/existing.json";
    const auto copied=root/"Assets/Scenes/copied.json";
    const auto blocked=root/"Assets/Scenes/blocked.json";
    if (std::filesystem::exists(copied)) std::filesystem::remove(copied);
    std::filesystem::create_directories(blocked);
    SceneRuntime::SceneLayout layout;
    SceneRuntime::ScenePlacement object;
    object.id="save-as-object";
    object.name="別名保存";
    object.SetModel("Assets/Models/Title/triangle.obj");
    layout.objects.push_back(object);
    layout.Save(original);
    layout.Save(existing);
    const auto originalJson=layout.Serialize();
    Editor::SceneDocument document(original);
    layout.objects[0].position[0]=12;
    bool rejected=false;
    try { document.SaveAs(layout,existing,false); }
    catch (const std::exception&) { rejected=true; }
    Check(rejected && document.Path()==original && SceneRuntime::SceneLayout::Load(existing).Serialize()==originalJson,
        "Save as requires explicit overwrite and preserves existing file and destination");
    rejected=false;
    try { document.SaveAs(layout,blocked,true); }
    catch (const std::exception&) { rejected=true; }
    Check(rejected && document.Path()==original && layout.objects[0].position[0]==12 && std::filesystem::is_directory(blocked),
        "Save as write failure preserves active destination, scene and target");
    document.SaveAs(layout,copied,false);
    Check(document.Path()==copied && !document.UnsavedNew() &&
        SceneRuntime::SceneLayout::Load(copied).Serialize()==layout.Serialize() &&
        SceneRuntime::SceneLayout::Load(original).Serialize()==originalJson,
        "Save as writes current edits to a new destination without changing original scene");
    rejected=false;
    try { layout.Save(existing,false); }
    catch (const std::exception&) { rejected=true; }
    Check(rejected && SceneRuntime::SceneLayout::Load(existing).Serialize()==originalJson,
        "atomic save commit refuses replacement when overwrite is not approved");
    document.SaveAs(layout,existing,true);
    Check(document.Path()==existing && SceneRuntime::SceneLayout::Load(existing).Serialize()==layout.Serialize(),
        "confirmed Save as replaces destination and changes active path");
    Check(Editor::SceneDocument::SaveTarget(root,"existing.json")==existing &&
        Editor::SceneDocument::SaveTarget(root,"新規コピー.JSON").parent_path()==root/"Assets/Scenes",
        "Save as accepts existing and UTF-8 targets for subsequent confirmation");
}

void ValidateConsoleLog()
{
    Engine::Log::ClearRecent();
    Engine::Log::Shutdown();
    Engine::Log::Info("before initialization");
    Check(Engine::Log::Recent().size()==1, "Console captures logs without an open file");
    const auto path=std::filesystem::absolute("generated/tests/console/console.log");
    Check(Engine::Log::Initialize(path), "Console diagnostic file opens");
    Engine::Log::ClearRecent();
    for (int index=0;index<503;++index) Engine::Log::Info("entry "+std::to_string(index));
    const auto entries=Engine::Log::Recent();
    Check(entries.size()==500 && entries.front().text.find("entry 3\n")!=std::string::npos &&
        entries.back().text.find("entry 502\n")!=std::string::npos, "Console retains the newest 500 logs in order");
    Check(entries.back().sequence-entries.front().sequence==499, "Console log sequence increases monotonically");
    Engine::Log::Error("モデル追加 Failed\nsecond line");
    const auto error=Engine::Log::Recent().back();
    const std::array<bool,4> all{true,true,true,true}, infoOnly{false,true,false,false};
    Check(Editor::ConsoleMatches(error,all,"failed") && Editor::ConsoleMatches(error,all,"モデル") &&
        !Editor::ConsoleMatches(error,infoOnly,"") && !Editor::ConsoleMatches(error,all,"not present"),
        "Console filters severity, ASCII case, UTF-8 and multiline messages");
    Check(!Editor::ConsoleMatches({0,static_cast<Engine::LogLevel>(99),"unknown"},all,""),
        "Console rejects unknown log levels safely");
    Engine::Log::ClearRecent();
    Check(Engine::Log::Recent().empty() && entries.size()==500, "Console clear preserves copied snapshots");
    Engine::Log::Warning("after clear");
    Check(Engine::Log::Recent().back().sequence>error.sequence, "Console clear does not reuse log sequence IDs");
    Engine::Log::Shutdown();
    std::ifstream input(path,std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(input)),{});
    Check(text.find("モデル追加 Failed")!=std::string::npos && text.find("after clear")!=std::string::npos,
        "Console clear preserves diagnostic file and subsequent writes");
    Engine::Log::ClearRecent();
}

void ValidateModelDrop()
{
    Engine::Camera camera;
    camera.SetPosition({3,10,-5});
    camera.SetRotation(0,0);
    camera.SetPerspective(DirectX::XM_PIDIV4,2,0.1f,2000);
    const Editor::SceneViewport viewport{120,80,800,400};
    const auto ground=Editor::ModelDropPosition(camera,viewport,520,450);
    Check(ground && std::abs((*ground)[1]-0.08f)<0.001f &&
        std::abs((*ground)[0]-3)<0.001f && (*ground)[2]>-5, "model drop ray intersects ground under cursor");
    const auto horizon=Editor::ModelDropPosition(camera,viewport,520,280);
    Check(horizon && std::abs((*horizon)[1]-10)<0.001f && (*horizon)[2]>3 && (*horizon)[2]<3.2f,
        "horizontal model drop falls back to eight units from near plane");
    const auto sky=Editor::ModelDropPosition(camera,viewport,520,100);
    Check(sky && (*sky)[1]>10, "sky drop uses forward ray instead of intersection behind camera");
    Check(!Editor::ModelDropPosition(camera,viewport,119,280) &&
        !Editor::ModelDropPosition(camera,{},520,280), "model drop rejects outside and hidden viewport");
    const Editor::SceneViewport moved{20,30,400,200};
    const auto sameRay=Editor::ModelDropPosition(camera,moved,220,215);
    Check(sameRay && ground && std::abs((*sameRay)[2]-(*ground)[2])<0.001f,
        "model drop position survives dock movement and viewport resize");
}

void ValidateProjectCatalog()
{
    const auto root=std::filesystem::absolute("generated/tests/project-catalog");
    for (const auto* path : {"Assets/Models/Title/building.obj","Assets/Models/Mesh.OBJ",
        "Assets/Scenes/title.json","Assets/Scenes/Nested/Second.JSON","Assets/Settings/settings.json",
        "Assets/Models/Title/building.mtl","Assets/Models/Title/texture.png",
        "Assets/Audio/click.WAV","Assets/Fonts/Test.ttf","Shaders/Test.HLSL","Shaders/Common/Test.hlsli"})
    {
        const auto destination=root/path;
        std::filesystem::create_directories(destination.parent_path());
        std::ofstream fixture(destination);
        fixture << "fixture";
    }
    Editor::ProjectCatalog catalog;
    Check(catalog.Scan(root) && catalog.Assets().size()==9, "Project lists typed assets and shaders with case-insensitive extensions");
    std::array<size_t,6> counts{};
    for (const auto& asset : catalog.Assets())
    {
        ++counts[static_cast<size_t>(asset.kind)];
        Check(!asset.path.is_absolute() && (Editor::ProjectCatalog::Text(asset.path).starts_with("Assets/") || Editor::ProjectCatalog::Text(asset.path).starts_with("Shaders/")),
            "Project keeps Content-relative asset paths");
    }
    Check(counts==std::array<size_t,6>{2,2,1,1,2,1}, "Project distinguishes models scenes textures audio shaders and fonts");
    const Editor::ProjectAsset nested{"Assets/Models/Title/building.obj",Editor::AssetKind::Model};
    Check(Editor::ProjectCatalog::Matches(nested,"Assets/Models/Title","") &&
        !Editor::ProjectCatalog::Matches(nested,"Assets/Models","") &&
        Editor::ProjectCatalog::Matches(nested,"Assets/Scenes","BUILDING") &&
        !Editor::ProjectCatalog::Matches(nested,"Assets","missing"), "Project folder browsing and global path search");
    Check(std::find(catalog.Folders().begin(),catalog.Folders().end(),"Assets/Models/Title")!=catalog.Folders().end() &&
        std::find(catalog.Folders().begin(),catalog.Folders().end(),"Assets/Settings")==catalog.Folders().end(),
        "Project tree contains the supported asset ancestors");
    const auto oldPath=catalog.Assets().front().path;
    Check(!catalog.Scan(root/"missing-root") && !catalog.Error().empty() && catalog.Assets().size()==9 &&
        catalog.Assets().front().path==oldPath, "failed refresh preserves previous Project catalog");
    Check(catalog.Scan(root) && catalog.Error().empty(), "successful refresh clears Project error");
    Editor::EditState state; state.Select("object"); state.MarkSaved();
    state.InspectAsset("Shaders/Test.HLSL");
    Check(!state.InspectedAsset().empty() && state.SelectedId()=="object" && !state.HasChanges(), "asset inspection preserves scene selection and dirty state");
    state.Select("object"); Check(state.InspectedAsset().empty(), "scene selection returns Inspector to object properties");
    const Editor::ProjectAsset shader{"Shaders/Test.HLSL",Editor::AssetKind::Shader};
    const auto info=Editor::AssetInfo::Read(root,shader);
    Check(info.error.empty() && info.bytes==7 && info.text=="fixture" && !info.truncated, "source asset metadata and read-only preview");
    { std::ofstream fixture(root/shader.path); fixture << std::string(9000,'x'); }
    const auto large=Editor::AssetInfo::Read(root,shader);
    Check(large.error.empty() && large.bytes==9000 && large.text.size()==8192 && large.truncated, "source preview is bounded independently of file size");
    { std::ofstream fixture(root/shader.path,std::ios::binary); fixture.write("a\0b",3); }
    const auto binary=Editor::AssetInfo::Read(root,shader);
    Check(!binary.error.empty() && binary.text.empty(), "binary source preview rejected without displaying partial data");
    const auto texture=Editor::AssetInfo::Read(root,{"Assets/Models/Title/texture.png",Editor::AssetKind::Texture});
    Check(texture.error.empty() && texture.bytes==7 && texture.text.empty(), "binary asset provides metadata without source decoding");
    Check(!Editor::AssetInfo::Read(root,{"../outside.hlsl",Editor::AssetKind::Shader}).error.empty() &&
        !Editor::AssetInfo::Read(root,{"Shaders/missing.hlsl",Editor::AssetKind::Shader}).error.empty(), "invalid and missing asset paths report preview errors");
}

void ValidateEditHistory()
{
    Editor::EditHistory history;
    history.Reset({"initial", "a"});
    history.Observe({"drag1", "a"},"drag");
    history.Observe({"drag2", "a"},"drag");
    Check(!history.CanUndo(), "ongoing drag is not a history entry");
    history.Observe({"drag2", "a"},{});
    Check(history.CanUndo() && history.Target(false).json=="initial", "drag becomes one undo entry");
    history.Saved("drag2");
    Check(!history.Dirty("drag2") && history.Dirty("initial"), "saved content determines dirty state");
    history.Applied(false);
    Check(history.CanRedo() && history.Target(true).selection=="a", "redo preserves selection");
    history.Observe({"branch", "b"},{});
    Check(!history.CanRedo() && history.Target(false).json=="initial", "new edit clears redo branch");
    history.Reset({"initial","a",{"a","b"}});
    history.Observe({"first","a",{"a","b"}},"inspector/position");
    history.Observe({"second","a",{"a","b"}},"inspector/rotation");
    history.Observe({"second","a",{"a","b"}},{});
    Check(history.Target(false).json=="first", "different inspector fields split history without idle frame");
    history.Applied(false);
    Check(history.Target(false).json=="initial", "first field remains its own undo entry");
    history.Observe({"first","a",{"a","b"}},"gizmo/move");
    history.Observe({"first","a",{"a","b"}},{});
    Check(history.CanRedo() && history.Target(true).json=="second", "unchanged interaction preserves redo branch");
    history.Observe({"third","a",{"a","b"}},"gizmo/move");
    history.Saved("third");
    Check(history.Target(false).json=="first" && !history.Dirty("third"), "save flushes pending interaction before marking baseline");
    history.Reset({"reloaded", ""});
    Check(!history.CanUndo() && !history.CanRedo() && !history.Dirty("reloaded"), "reload resets history and saved state");
    for (int i=0;i<150;++i) history.Observe({std::to_string(i), ""},{});
    int undoCount=0;
    while (history.CanUndo()) { history.Applied(false); ++undoCount; }
    Check(undoCount==100, "history is bounded to 100 edits");
}

void ValidateFocusSelection()
{
    using namespace DirectX;
    Engine::Camera camera;
    camera.SetRotation(0.7f,0.25f);
    std::array<std::array<float,3>,8> corners{};
    for (int i=0;i<8;++i) corners[i]={30.0f+(i&1 ? 3.0f : -3.0f),
        7.0f+(i&2 ? 6.0f : -6.0f),50.0f+(i&4 ? 2.0f : -2.0f)};
    for (float aspect : {16.0f/9.0f,1.0f,9.0f/16.0f})
    {
        camera.SetPerspective(XM_PIDIV4,aspect,0.1f,220.0f);
        const auto position=Editor::FocusPosition(corners,camera);
        Check(position.has_value(), "selected model fits within camera range");
        camera.SetPosition(*position);
        for (const auto& corner : corners)
        {
            XMFLOAT3 projected;
            XMStoreFloat3(&projected,XMVector3TransformCoord(XMVectorSet(corner[0],corner[1],corner[2],1),
                camera.GetViewMatrix()*camera.GetProjectionMatrix()));
            Check(std::abs(projected.x)<1 && std::abs(projected.y)<1 && projected.z>0 && projected.z<1,
                "focus fits every bounds corner in landscape and portrait views");
        }
    }
    corners[0][0]=NAN;
    Check(!Editor::FocusPosition(corners,camera), "nonfinite focus bounds rejected");
    corners[0][0]=-10000;
    Check(!Editor::FocusPosition(corners,camera), "oversized focus bounds rejected");
}

void ValidatePlayState()
{
    Editor::PlayState state;
    Check(state.Current()==Editor::PlayState::Mode::Editing && state.IsEditing() && state.CanPlay() &&
        !state.CanPause() && !state.CanStop() && state.Elapsed()==0 && state.Updates()==0, "play state starts in editing");
    Check(!state.Pause() && !state.Stop() && !state.Advance(0.25), "editing cannot pause stop or advance game time");
    Check(state.Play() && !state.IsEditing() && !state.CanPlay() && state.CanPause() && state.CanStop() &&
        std::string_view(state.Label())=="再生中", "Play enters playing and locks editing");
    Check(state.Advance(0.25) && state.Elapsed()==0.25 && state.Updates()==1, "playing accepts a game tick");
    Check(!state.Play() && state.Elapsed()==0.25 && state.Updates()==1, "repeated Play does not reset active timing");
    Check(state.Pause() && state.Current()==Editor::PlayState::Mode::Paused && !state.IsEditing() &&
        state.CanPlay() && !state.CanPause() && state.CanStop() && std::string_view(state.Label())=="一時停止中", "Pause freezes playing while keeping editing locked");
    Check(!state.Advance(0.5) && !state.Pause() && state.Elapsed()==0.25 && state.Updates()==1, "paused time and update count do not advance");
    Check(state.Play() && state.Advance(0.5) && state.Elapsed()==0.75 && state.Updates()==2, "Resume continues existing game time");
    Check(!state.Advance(0) && !state.Advance(-1) && !state.Advance(NAN) && !state.Advance(INFINITY) &&
        state.Elapsed()==0.75 && state.Updates()==2, "invalid deltas leave playback timing intact");
    Check(state.Stop() && state.IsEditing() && state.Elapsed()==0 && state.Updates()==0 &&
        std::string_view(state.Label())=="編集中", "Stop from playing restores editing and resets timing");
    Check(state.Play() && state.Advance(0.125) && state.Pause() && state.Stop() && state.IsEditing() &&
        state.Elapsed()==0 && state.Updates()==0, "Stop from paused resets the next session");
    Check(!state.Step() && state.Play() && !state.Step() && state.Pause() && state.Step() && state.Step() &&
        state.Current()==Editor::PlayState::Mode::Paused && state.Updates()==2 &&
        std::abs(state.Elapsed()-2*Editor::PlayState::StepSeconds)<1e-12 && state.Stop(), "only paused state accepts repeatable fixed frame steps");
    Check(state.Play() && state.Advance(std::numeric_limits<double>::max()), "finite large tick accepted");
    Check(!state.Advance(std::numeric_limits<double>::max()) && state.Elapsed()==std::numeric_limits<double>::max() &&
        state.Updates()==1, "overflowing elapsed time is rejected atomically");
    Check(state.Stop() && state.Play() && state.Advance(0.25) && state.Elapsed()==0.25 && state.Updates()==1,
        "new session after overflow starts cleanly");
}

void ValidateDeferredClose()
{
    Engine::Window window;
    Check(window.Create(L"Close request validation",320,240), "close validation window created");
    int exitCode=99;
    SendMessageW(window.GetHandle(),WM_CLOSE,0,0);
    Check(window.ProcessMessages(exitCode,true) && IsWindow(window.GetHandle()),
        "deferred close keeps the window alive for confirmation");
    Check(window.TakeCloseRequest() && !window.TakeCloseRequest(), "close request is consumed once");
    Check(window.ProcessMessages(exitCode,true), "cancelled close permits further frames");
    SendMessageW(window.GetHandle(),WM_CLOSE,0,0);
    Check(window.ProcessMessages(exitCode,true) && window.TakeCloseRequest(), "close can be requested again after cancel");
    SendMessageW(window.GetHandle(),WM_CLOSE,0,0);
    Check(!window.ProcessMessages(exitCode) && exitCode==0, "ordinary game windows still close without confirmation");
}
int main()
{
    try
    {
        ValidateTransformDecomposition();
        ValidateSceneViewport();
        ValidateProjectCatalog();
        ValidateModelDrop();
        ValidateConsoleLog();
        ValidateSaveAs();
        ValidateMultiSelection();
        ValidateEditHistory();
        ValidateFocusSelection();
        ValidatePlayState();
        ValidateDeferredClose();
        ValidateEditorCamera();
        UiValidation::SchemaAndLayout();
        ValidateSceneLayout();
        ValidateEditorAcceptanceScene();
        ValidateAssetChangeBatching();
        ValidateComponentSchema();
        ValidateParentData();
        ValidateHierarchyRows();
        ValidateSceneTransforms();
        ValidateSceneFiles();
        ValidateDiagnostics();
        ValidateTitleMenu();
        ValidateTitleAnimation();
        ValidateSettings();
        ValidatePressAnyTitle();
        ValidateTitleAudio();
        ValidateRenderTexture();
        ValidateTitle();
        ValidateMirroredMesh();
        std::cout << "PASS: diagnostics location/overrides, title menu/settings/rendering, mirrored mesh visibility and backface culling\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
