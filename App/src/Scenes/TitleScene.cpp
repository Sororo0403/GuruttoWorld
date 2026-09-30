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
    TitleScene::TitleScene(std::filesystem::path root, bool playIntro) : root_(std::move(root)), menu_(playIntro) {}

    bool TitleScene::Initialize(Engine::DirectX12Renderer& renderer)
    {
        menu_.LoadSettings(GameSettings::Load(GameSettings::UserPath()));
        if (!environment_.Initialize(renderer, root_)) return false;
        environment_.Update(0.0, menu_.GetSettings().backgroundMotion, false);
        return ui_.Initialize(renderer, root_);
    }
    TitleMenuInput TitleScene::ReadMenuInput(const Engine::Keyboard& keyboard) const
    {
        TitleMenuInput input;
        input.active = keyboard.IsActive();
        input.gamepadConnected = gamepad_.IsConnected();
        input.keyboardButtons = ReadKeyboardButtons(keyboard);
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
        if (action == TitleMenuAction::SaveSettings)
            menu_.CompleteSave(menu_.GetSettings().Save(GameSettings::UserPath()));
        audio_.Update(root_, menu_, keyboard.IsActive(), deltaSeconds);
        environment_.Update(deltaSeconds, menu_.GetSettings().backgroundMotion,
            keyboard.IsActive() && menu_.TransitionProgress() == 0.0f);
        if (action == TitleMenuAction::Start) return "Game";
        // WM_QUIT を既存のメッセージループへ送り、GPU 完了待ちと通常の破棄を通します。
        if (action == TitleMenuAction::Exit) PostQuitMessage(0);
        return {};
    }
    Engine::RenderResult TitleScene::Draw(Engine::DirectX12Renderer& renderer)
    {
        return renderer.Render({ 0.66f, 0.79f, 0.83f, 1.0f }, [&](ID3D12GraphicsCommandList* commands, float)
        {
            environment_.Draw(commands, renderer.GetWidth(), renderer.GetHeight());
            ui_.Draw(commands, renderer.GetWidth(), renderer.GetHeight(), menu_);
        });
    }
}
