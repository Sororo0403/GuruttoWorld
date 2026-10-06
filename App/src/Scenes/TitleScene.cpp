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
    TitleScene::TitleScene(std::filesystem::path root, bool playIntro) : root_(std::move(root)), menu_(playIntro, true) {}

    bool TitleScene::Initialize(Engine::DirectX12Renderer& renderer)
    {
        menu_.LoadSettings(GameSettings::Load(GameSettings::UserPath()));
        std::string error;
        if (!environment_.Initialize(renderer,root_,root_/"Assets/Scenes/TitleStreet.json",error)) return false;
        environment_.Update(0.0, menu_.GetSettings().backgroundMotion, false);
        return ui_.Initialize(renderer, root_);
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
        const auto action = menu_.Update(input, deltaSeconds);
        audio_.Update(root_, menu_, keyboard.IsActive(), deltaSeconds);
        environment_.Update(deltaSeconds, menu_.GetSettings().backgroundMotion,
            keyboard.IsActive() && menu_.TransitionProgress() == 0.0f);
        if (action == TitleMenuAction::Start) return "Game";
        return {};
    }
    Engine::RenderResult TitleScene::Draw(Engine::DirectX12Renderer& renderer)
    {
        return renderer.Render(environment_.World().Layout().settings.background, [&](ID3D12GraphicsCommandList* commands, float)
        {
            environment_.Draw(commands, renderer.GetWidth(), renderer.GetHeight());
            ui_.Draw(commands, renderer.GetWidth(), renderer.GetHeight(), menu_);
        });
    }
}
