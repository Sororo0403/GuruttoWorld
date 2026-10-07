#include "AuthoredScene.h"
#include <Engine/Input/Keyboard.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <Engine/Core/Log.h>
#include "GameSettings.h"
namespace App {
bool AuthoredScene::Initialize(Engine::DirectX12Renderer& renderer) {
    std::string error;
    if(!environment_.Initialize(renderer,root_,root_/scene_,error)) {Engine::Log::Error(error); return false;}
    if(!environment_.StartAudio(root_,error)) Engine::Log::Warning(error);
    environment_.Ui().values["volumeGain"]=GameSettings::Load(GameSettings::UserPath()).volume/10.0f;
    return true;
}
std::string AuthoredScene::Update(double seconds,const Engine::Keyboard& keyboard) {
    const bool active=keyboard.IsActive();
    gamepad_.Update(active);
    environment_.Update(seconds,true,active); environment_.UpdateAudio(active);
    auto& ui=environment_.Ui();
    if(!active) {ready_=false; down_=false; ui.pressed.clear(); ui.hovered.clear(); return {};}
    POINT cursor{}; GetCursorPos(&cursor); ScreenToClient(keyboard.WindowHandle(),&cursor);
    ui.hovered=SceneRuntime::SceneUi::Hit(environment_.World().Layout(),width_,height_,static_cast<float>(cursor.x),static_cast<float>(cursor.y),ui);
    const bool down=(GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0;
    if(ready_ && down && !down_) ui.pressed=ui.hovered;
    SceneRuntime::UiEvent event;
    const std::pair<const char*,unsigned int> shortcuts[]={{"space",DIK_SPACE},{"escape",DIK_ESCAPE},{"1",DIK_1},{"2",DIK_2}};
    if(ready_) for(const auto& [key,scan]:shortcuts) {
        const bool pressed=keyboard.IsPressed(scan) || (std::string_view(key)=="space" && gamepad_.IsPressed(XINPUT_GAMEPAD_A));
        if(!pressed) continue;
        const auto object=SceneRuntime::SceneUi::Shortcut(environment_.World().Layout(),key,width_,height_,ui);
        if(!object.empty()) {event=environment_.Click(object); break;}
    }
    if(ready_ && !down && down_) {
        if(!ui.pressed.empty() && ui.pressed==ui.hovered) event=environment_.Click(ui.pressed);
        ui.pressed.clear();
    }
    down_=down; ready_=true;
    if(event.action=="quit") PostMessageW(keyboard.WindowHandle(),WM_CLOSE,0,0);
    return event.action=="loadScene"?event.target:std::string{};
}
Engine::RenderResult AuthoredScene::Draw(Engine::DirectX12Renderer& renderer) {
    width_=renderer.GetWidth(); height_=renderer.GetHeight();
    return renderer.Render(environment_.World().Layout().settings.background,[&](auto* commands,float) {environment_.Draw(commands,width_,height_);});
}
}
