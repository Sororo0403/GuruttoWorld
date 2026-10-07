#include "TitleScene.h"
#include <Engine/Input/Keyboard.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <utility>

namespace
{
    unsigned int ReadKeyboardButtons(const Engine::Keyboard& keyboard)
    {
        unsigned int buttons = 0;
        if (keyboard.IsDown(DIK_UP) || keyboard.IsDown(DIK_W)) buttons |= App::MenuUp;
        if (keyboard.IsDown(DIK_DOWN) || keyboard.IsDown(DIK_S)) buttons |= App::MenuDown;
        if (keyboard.IsDown(DIK_LEFT) || keyboard.IsDown(DIK_A)) buttons |= App::MenuLeft;
        if (keyboard.IsDown(DIK_RIGHT) || keyboard.IsDown(DIK_D)) buttons |= App::MenuRight;
        if (keyboard.IsDown(DIK_ESCAPE)) buttons |= App::MenuBack;
        if (keyboard.IsDown(DIK_RETURN)) buttons |= App::MenuConfirm;
        return buttons;
    }
}

namespace App
{
    TitleScene::TitleScene(std::filesystem::path root, bool playIntro) : root_(std::move(root)), menu_(playIntro, false, true) {}

    bool TitleScene::Initialize(Engine::DirectX12Renderer& renderer)
    {
        menu_.LoadSettings(GameSettings::Load(GameSettings::UserPath()));
        std::string error;
        if (!environment_.Initialize(renderer,root_,root_/"Assets/Scenes/TitleStreet.json",error)) return false;
        environment_.Update(0.0, menu_.GetSettings().backgroundMotion, false);
        menu_.SetTransitionDuration(environment_.Ui().Value("startDuration",0.32f));
        if (menu_.IntroProgress()==1.0f)
            environment_.SeekAnimation(environment_.Ui().Value("introDuration",0),0);
        SyncUi();
        return true;
    }
    TitleMenuInput TitleScene::ReadMenuInput(const Engine::Keyboard& keyboard) const
    {
        TitleMenuInput input;
        input.active = keyboard.IsActive();
        input.gamepadConnected = gamepad_.IsConnected();
        input.keyboardButtons = ReadKeyboardButtons(keyboard);
        for (unsigned int key = 0; key < 256; ++key)
            input.anyButtonPressed |= keyboard.IsPressed(key);
        constexpr unsigned int PadButtons = XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN |
            XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT | XINPUT_GAMEPAD_START |
            XINPUT_GAMEPAD_BACK | XINPUT_GAMEPAD_LEFT_THUMB | XINPUT_GAMEPAD_RIGHT_THUMB |
            XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_RIGHT_SHOULDER | XINPUT_GAMEPAD_A |
            XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_Y;
        for (unsigned int bit = 1; bit <= XINPUT_GAMEPAD_Y; bit <<= 1)
            if ((PadButtons & bit) != 0) input.anyButtonPressed |= gamepad_.IsPressed(static_cast<WORD>(bit));
        if (gamepad_.IsDown(XINPUT_GAMEPAD_DPAD_UP)) input.gamepadButtons |= MenuUp;
        if (gamepad_.IsDown(XINPUT_GAMEPAD_DPAD_DOWN)) input.gamepadButtons |= MenuDown;
        if (gamepad_.IsDown(XINPUT_GAMEPAD_A)) input.gamepadButtons |= MenuConfirm;
        if (gamepad_.IsDown(XINPUT_GAMEPAD_DPAD_LEFT)) input.gamepadButtons |= MenuLeft;
        if (gamepad_.IsDown(XINPUT_GAMEPAD_DPAD_RIGHT)) input.gamepadButtons |= MenuRight;
        if (gamepad_.IsDown(XINPUT_GAMEPAD_B)) input.gamepadButtons |= MenuBack;
        input.stickX = gamepad_.GetLeftStick()[0];
        input.stickY = gamepad_.GetLeftStick()[1];
        return input;
    }
    std::string TitleScene::Update(double deltaSeconds, const Engine::Keyboard& keyboard)
    {
        gamepad_.Update(keyboard.IsActive());
        const auto input = ReadMenuInput(keyboard);
        auto action = menu_.Update(input, deltaSeconds);
        SyncUi();
        const auto pointerAction=UpdatePointer(keyboard);
        if(pointerAction!=TitleMenuAction::None) action=pointerAction;
        if(action==TitleMenuAction::SaveSettings) menu_.CompleteSave(menu_.GetSettings().Save(GameSettings::UserPath()));
        if(action==TitleMenuAction::Exit) PostMessageW(keyboard.WindowHandle(),WM_CLOSE,0,0);
        if (menu_.GetCue()==TitleMenuCue::Confirm && !menu_.IsSettingsOpen() &&
            menu_.GetSelected()==TitleMenuItem::Start)
        {
            environment_.SeekAnimation(environment_.Ui().Value("introDuration",0),
                static_cast<float>(environment_.MotionSeconds()));
            environment_.Ui().values["startRequested"]=1;
        }
        SyncUi();
        environment_.Ui().hovered=hovered_; environment_.Ui().pressed=pressed_;
        audio_.Update(environment_,root_, menu_, keyboard.IsActive(), deltaSeconds);
        environment_.Update(deltaSeconds, menu_.GetSettings().backgroundMotion,
            keyboard.IsActive());
        if (action == TitleMenuAction::Start) return "Game";
        if(!requestedScene_.empty()) return std::exchange(requestedScene_,{});
        return {};
    }
    void TitleScene::SyncUi()
    {
        for(const auto& [key,value]:TitleUi::State(menu_).values) environment_.Ui().values[key]=value;
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
        if (hovered_=="world-start") menu_.SelectUi(TitleMenuItem::Start);
        if (hovered_=="world-config") menu_.SelectUi(TitleMenuItem::Settings);
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
