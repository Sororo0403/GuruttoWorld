#include "AuthoredScene.h"
#include <Engine/Input/Keyboard.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <Engine/Core/Log.h>
#include <Engine/Core/Profiler.h>
#include "GameSettings.h"
#include <SceneRuntime/ProjectSettings.h>
#include <Engine/Platform/Window.h>
namespace App {
bool AuthoredScene::Initialize(Engine::DirectX12Renderer& renderer) {
    std::string error;
    input_.SetBindings(SceneRuntime::ProjectSettings::Load(root_).inputActions);
    const bool loaded=!environment_.Initialized() ? environment_.Initialize(renderer,root_,root_/scene_,error) : environment_.LoadScene(scene_,false,error);
    if(!loaded) {Engine::Log::Error(error); return false;}
    if(!environment_.StartAudio(root_,error)) Engine::Log::Warning(error);
    environment_.Ui().values["volumeGain"]=GameSettings::Load(GameSettings::UserPath()).Gain();
    return true;
}
std::string AuthoredScene::Update(double seconds,const Engine::Keyboard& keyboard) {
    Engine::CpuScope authoredUpdate("App authored update");
    const bool active=keyboard.IsActive();
    const bool gameplayInput=active && environment_.Ui().focused.empty();
    {
        Engine::CpuScope inputUpdate("App input");
        gamepad_.Update(active && !keyboard.GetDiagnosticPointer());
        input_.Update(Engine::InputActions::Capture(keyboard,&gamepad_,true));
        if(gameplayInput) environment_.SetInputActions(input_.Values(),input_.PressedValues());
        else environment_.SetInputActions({},{});
    }
    const bool simulationActive=active || simulateInactive_;
    const auto& playerInputs=environment_.World().Layout().settings.playerInputs;
    if(simulationActive) environment_.MovePlayers(seconds,
        gameplayInput?input_.Value(playerInputs[0])-input_.Value(playerInputs[1]):0,
        gameplayInput?input_.Value(playerInputs[2])-input_.Value(playerInputs[3]):0,gameplayInput && input_.Pressed(playerInputs[4]));
    environment_.Update(seconds,true,simulationActive);
    { Engine::CpuScope audioUpdate("App audio"); environment_.UpdateAudio(simulationActive); }
    return UpdateUi(keyboard,active);
}
std::string AuthoredScene::UpdateUi(const Engine::Keyboard &keyboard, bool active) {
    Engine::CpuScope uiUpdate("App UI input");
    auto &ui = environment_.Ui();
    const auto characters = Engine::Window::ConsumeTextInput(keyboard.WindowHandle());
    const auto *diagnosticPointer = keyboard.GetDiagnosticPointer();
    const auto wheel = diagnosticPointer ? diagnosticPointer->wheel : Engine::Window::ConsumeMouseWheel(keyboard.WindowHandle());
    if (!active) {ready_ = false; down_ = false; ui.pressed.clear(); ui.hovered.clear(); ui.focused.clear(); return {};}
    POINT cursor{};
    if (diagnosticPointer) { cursor.x = diagnosticPointer->x; cursor.y = diagnosticPointer->y; }
    else { GetCursorPos(&cursor); ScreenToClient(keyboard.WindowHandle(), &cursor); }
    const bool down = diagnosticPointer ? (diagnosticPointer->buttons & 1) != 0 : (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    auto event = environment_.UiPointer(width_, height_, static_cast<float>(cursor.x), static_cast<float>(cursor.y), ready_ && down, ready_ && down && !down_, ready_ && !down && down_, wheel);
    if (!diagnosticPointer) for (const auto character : characters) environment_.UiTextInput(character);
    ApplyShortcut(event);
    down_ = down; ready_ = true;
    if (event.action == "quit") PostMessageW(keyboard.WindowHandle(), WM_CLOSE, 0, 0);
    return event.action == "loadScene" ? event.target : std::string{};

}

void AuthoredScene::ApplyShortcut(SceneRuntime::UiEvent &event) {
    const auto &ui = environment_.Ui();
    if (ready_ && ui.focused.empty()) for (const auto &[name, binding] : input_.GetBindings()) {
            static_cast<void>(binding);
            if (!input_.Pressed(name)) continue;
            const auto object = SceneRuntime::SceneUi::Shortcut(environment_.World().Layout(), "action:" + name, width_, height_, ui);
            if (!object.empty()) {event = environment_.Click(object); break;}
        }
}
Engine::RenderResult AuthoredScene::Draw(Engine::DirectX12Renderer& renderer) {
    width_=renderer.GetWidth(); height_=renderer.GetHeight();
    std::string error; if(!environment_.PrepareUi(renderer,root_,error)) {Engine::Log::Error(error); return Engine::RenderResult::Failed;}
    return renderer.Render(environment_.World().Layout().settings.background,[&](auto* commands,float) {environment_.Draw(commands,width_,height_);});
}
}
