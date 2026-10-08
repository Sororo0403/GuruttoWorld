#include "TitleScene.h"
#include "TitleBindings.h"
#include <Engine/Core/Log.h>
#include <Engine/Input/Keyboard.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <utility>
#include <SceneRuntime/ProjectSettings.h>

namespace App
{
    TitleScene::TitleScene(std::filesystem::path root, bool playIntro,std::filesystem::path scene) : root_(std::move(root)), scene_(std::move(scene)), menu_(playIntro, false, false) {}

    bool TitleScene::Initialize(Engine::DirectX12Renderer& renderer)
    {
        input_.SetBindings(SceneRuntime::ProjectSettings::Load(root_).inputActions);
        std::string error;
        if (!environment_.Initialize(renderer,root_,root_/scene_,error)) return false;
        menu_.Configure(TitleBindings::EffectiveConfiguration(environment_.World().Layout()));
        const auto settings=GameSettings::Load(GameSettings::UserPath());
        if(settings.loaded) menu_.LoadSettings(settings);
        environment_.Update(0.0, menu_.GetSettings().backgroundMotion, false);
        menu_.SetTransitionDuration(environment_.Ui().Value("startDuration",0.32f));
        menu_.SetPresentationDurations(environment_.Ui().Value("introDuration",0.65f),environment_.Ui().Value("selectionDuration",0.16f));
        menu_.SetItems(TitleBindings::Items(environment_.World().Layout()));
        if (menu_.IntroProgress()==1.0f)
            environment_.SeekAnimation(environment_.Ui().Value("introDuration",0),0);
        SyncUi();
        return true;
    }
    TitleMenuInput TitleScene::ReadMenuInput(const Engine::Keyboard& keyboard)
    {
        input_.Update(Engine::InputActions::Capture(keyboard,&gamepad_,true));
        auto input=TitleBindings::Input(menu_.Configuration(),input_,keyboard.IsActive());
        input.gamepadConnected=gamepad_.IsConnected();
        for (unsigned int key = 0; key < 256; ++key)
            input.anyButtonPressed |= keyboard.IsPressed(key);
        constexpr unsigned int PadButtons = XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN |
            XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT | XINPUT_GAMEPAD_START |
            XINPUT_GAMEPAD_BACK | XINPUT_GAMEPAD_LEFT_THUMB | XINPUT_GAMEPAD_RIGHT_THUMB |
            XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_RIGHT_SHOULDER | XINPUT_GAMEPAD_A |
            XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_Y;
        for (unsigned int bit = 1; bit <= XINPUT_GAMEPAD_Y; bit <<= 1)
            if ((PadButtons & bit) != 0) input.anyButtonPressed |= gamepad_.IsPressed(static_cast<WORD>(bit));
        return input;
    }
    std::string TitleScene::Update(double deltaSeconds, const Engine::Keyboard& keyboard)
    {
        gamepad_.Update(keyboard.IsActive());
        const auto input = ReadMenuInput(keyboard);
        const float previousIntro=menu_.IntroProgress();
        auto action = menu_.Update(input, deltaSeconds);
        SyncUi();
        const auto pointerAction=UpdatePointer(keyboard);
        if(previousIntro<1 && menu_.IntroProgress()==1)
            environment_.SeekAnimation(environment_.Ui().Value("introDuration",0),static_cast<float>(environment_.MotionSeconds()));
        if(pointerAction!=TitleMenuAction::None) action=pointerAction;
        if(action==TitleMenuAction::SaveSettings) menu_.CompleteSave(menu_.GetSettings().Save(GameSettings::UserPath()));
        if(action==TitleMenuAction::Exit) PostMessageW(keyboard.WindowHandle(),WM_CLOSE,0,0);
        if (menu_.GetCue()==TitleMenuCue::Confirm && !menu_.IsSettingsOpen() &&
            menu_.SelectedEntry() && menu_.SelectedEntry()->action=="loadScene")
        {
            environment_.SeekAnimation(environment_.Ui().Value("introDuration",0),
                static_cast<float>(environment_.MotionSeconds()));
            environment_.Ui().values["startRequested"]=1;
        }
        std::string assignments; if(menu_.TakeAssignments(assignments)) environment_.Ui().Assign(assignments);
        SyncUi();
        environment_.Ui().hovered=hovered_; environment_.Ui().pressed=pressed_;
        audio_.Update(environment_,root_, menu_, keyboard.IsActive(), deltaSeconds);
        environment_.Update(deltaSeconds, menu_.GetSettings().backgroundMotion,
            keyboard.IsActive());
        if (action == TitleMenuAction::Start) {
            const auto scene=menu_.PendingTarget();
            if(scene.empty()) Engine::Log::Warning("Menu entry has no scene target. Set the target in Canvas > Menu configuration.");
            return scene;
        }
        if(!requestedScene_.empty()) return std::exchange(requestedScene_,{});
        return {};
    }
    void TitleScene::SyncUi()
    {
        for(const auto& [key,value]:TitleUi::State(menu_,environment_.Ui()).values) environment_.Ui().values[key]=value;
    }
    TitleMenuAction TitleScene::UpdatePointer(const Engine::Keyboard& keyboard)
    {
        if(!keyboard.IsActive()) {mouseReady_=false; mouseDown_=false; pressed_.clear(); hovered_.clear(); return TitleMenuAction::None;}
        POINT cursor{}; GetCursorPos(&cursor); ScreenToClient(keyboard.WindowHandle(),&cursor);
        const bool down=(GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0;
        const auto& state=environment_.Ui();
        const auto previousHover=hovered_;
        hovered_=SceneRuntime::SceneUi::Hit(environment_.World().Layout(),width_,height_,static_cast<float>(cursor.x),static_cast<float>(cursor.y),state);
        SelectHovered(previousHover);
        auto action=TitleMenuAction::None;
        if(mouseReady_ && down && !mouseDown_) pressed_=hovered_;
        if(mouseReady_ && !down && mouseDown_) {
            if(!pressed_.empty() && pressed_==hovered_) {
                const auto event=environment_.Click(pressed_);
                if(!event.event.empty()) action=menu_.ActivateUi(event.event);
                else if(event.action=="quit") PostMessageW(keyboard.WindowHandle(),WM_CLOSE,0,0);
                else if(event.action=="loadScene") requestedScene_=event.target;
            }
            pressed_.clear();
        }
        mouseDown_=down; mouseReady_=true; return action;
    }
    void TitleScene::SelectHovered(const std::string& previousHover)
    {
        if (hovered_==previousHover) return;
        for(const auto& object:environment_.World().Layout().objects) if(object.id==hovered_)
            if(const auto item=TitleBindings::Item(object)) menu_.SelectUi(*item);
    }
    Engine::RenderResult TitleScene::Draw(Engine::DirectX12Renderer& renderer)
    {
        width_=renderer.GetWidth(); height_=renderer.GetHeight();
        return renderer.Render(environment_.World().Layout().settings.background, [&](ID3D12GraphicsCommandList* commands, float)
        {
            environment_.Draw(commands, renderer.GetWidth(), renderer.GetHeight());

        });
    }
}
