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
    playback_.Initialize(environment_,root_,settingsPath_,playIntro_);
    if (!environment_.StartScripts(error)) {Engine::Log::Error(error);return false;}
    return true;
}
std::string AuthoredScene::Update(double seconds,const Engine::Keyboard& keyboard) {
    Engine::CpuScope authoredUpdate("App authored update");
    const bool active=keyboard.IsActive(),simulationActive=active || simulateInactive_;
    gamepad_.Update(active && !keyboard.GetDiagnosticPointer());
    const auto snapshot=Engine::InputActions::Capture(keyboard,&gamepad_,true);
    input_.Update(snapshot);
    if(simulationActive) playback_.PrepareInput(environment_,input_,active,seconds,&snapshot);
    UpdateUi(keyboard,active);
    const auto command=playback_.Update(environment_,seconds,simulationActive);
    if(command && command->action=="quit") PostMessageW(keyboard.WindowHandle(),WM_CLOSE,0,0);
    return {};
}
void AuthoredScene::UpdateUi(const Engine::Keyboard &keyboard, bool active) {
    Engine::CpuScope uiUpdate("App UI input");
    const auto characters = Engine::Window::ConsumeTextInput(keyboard.WindowHandle());
    const auto *diagnosticPointer = keyboard.GetDiagnosticPointer();
    const auto wheel = diagnosticPointer ? diagnosticPointer->wheel : Engine::Window::ConsumeMouseWheel(keyboard.WindowHandle());
    if (!active) {playback_.Pointer(environment_,width_,height_,{},false);return;}
    POINT cursor{};
    if (diagnosticPointer) { cursor.x = diagnosticPointer->x; cursor.y = diagnosticPointer->y; }
    else { GetCursorPos(&cursor); ScreenToClient(keyboard.WindowHandle(), &cursor); }
    const bool down = diagnosticPointer ? (diagnosticPointer->buttons & 1) != 0 : (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    playback_.Shortcut(environment_,input_,width_,height_,active);
    playback_.Pointer(environment_,width_,height_,{static_cast<float>(cursor.x),static_cast<float>(cursor.y),down,false,false,wheel},active);
    if (!diagnosticPointer) for (const auto character : characters) environment_.UiTextInput(character);


}

Engine::RenderResult AuthoredScene::Draw(Engine::DirectX12Renderer& renderer) {
    width_=renderer.GetWidth(); height_=renderer.GetHeight();
    std::string error; if(!environment_.Prepare(renderer,root_,error)) {Engine::Log::Error(error); return Engine::RenderResult::Failed;}
    return renderer.Render(environment_.World().Layout().settings.background,[&](auto* commands,float) {environment_.Draw(commands,width_,height_);});
}
}
