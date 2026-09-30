#include "TitleMenu.h"
#include <cmath>
#include <algorithm>

namespace App
{
    TitleMenuAction TitleMenu::Update(const TitleMenuInput& input)
    {
        if (!input.active)
        {
            ready_ = false;
            return TitleMenuAction::None;
        }
        unsigned int stick = stickPrevious_;
        if (!input.gamepadConnected || !std::isfinite(input.stickY) || std::abs(input.stickY) <= 0.3f) stick = 0;
        else if (input.stickY >= 0.55f) stick = MenuUp;
        else if (input.stickY <= -0.55f) stick = MenuDown;
        unsigned int stickX = stickXPrevious_;
        if (!input.gamepadConnected || !std::isfinite(input.stickX) || std::abs(input.stickX) <= 0.3f) stickX = 0;
        else if (input.stickX >= 0.55f) stickX = MenuRight;
        else if (input.stickX <= -0.55f) stickX = MenuLeft;
        const unsigned int pad = input.gamepadConnected ? input.gamepadButtons : 0;
        const unsigned int keyboardPressed = ready_ ? input.keyboardButtons & ~keyboardPrevious_ : 0;
        unsigned int padPressed = 0;
        if (ready_ && input.gamepadConnected && connectedPrevious_)
        {
            padPressed = pad & ~gamepadPrevious_;
            if (stick != stickPrevious_) padPressed |= stick;
            if (stickX != stickXPrevious_) padPressed |= stickX;
        }
        keyboardPrevious_ = input.keyboardButtons;
        gamepadPrevious_ = pad;
        stickPrevious_ = stick;
        stickXPrevious_ = stickX;
        connectedPrevious_ = input.gamepadConnected;
        ready_ = true;
        if (!input.gamepadConnected || keyboardPressed != 0) usesGamepad_ = false;
        else if (padPressed != 0) usesGamepad_ = true;
        if (finished_) return TitleMenuAction::None;
        const unsigned int pressed = keyboardPressed | padPressed;
        if (settingsOpen_ && (pressed & MenuBack))
        {
            draft_ = saved_;
            settingsOpen_ = false;
            saveFailed_ = false;
            return TitleMenuAction::None;
        }
        const unsigned int direction = pressed & (MenuUp | MenuDown);
        if (direction != 0)
        {
            if (direction != (MenuUp | MenuDown))
            {
                const int step = direction == MenuDown ? 1 : 2;
                if (settingsOpen_) settingsRow_ = (settingsRow_ + step) % 3;
                else selected_ = static_cast<TitleMenuItem>((static_cast<int>(selected_) + step) % 3);
            }
            // 選択移動と同じフレームの決定は無視し、意図せぬ終了を防ぎます。
            return TitleMenuAction::None;
        }
        if (settingsOpen_)
        {
            const unsigned int horizontal = pressed & (MenuLeft | MenuRight);
            if (horizontal != 0)
            {
                if (horizontal != (MenuLeft | MenuRight))
                {
                    if (settingsRow_ == 0) draft_.volume = std::clamp(draft_.volume + (horizontal == MenuRight ? 1 : -1), 0, 10);
                    if (settingsRow_ == 1) draft_.backgroundMotion = horizontal == MenuRight;
                }
                return TitleMenuAction::None;
            }
            if (pressed & MenuConfirm)
            {
                if (settingsRow_ == 1) draft_.backgroundMotion = !draft_.backgroundMotion;
                if (settingsRow_ == 2) return TitleMenuAction::SaveSettings;
            }
            return TitleMenuAction::None;
        }
        if ((pressed & MenuConfirm) != 0)
        {
            if (selected_ == TitleMenuItem::Settings)
            {
                settingsOpen_ = true;
                settingsRow_ = 0;
                draft_ = saved_;
                saveFailed_ = false;
                return TitleMenuAction::None;
            }
            finished_ = true;
            return selected_ == TitleMenuItem::Start ? TitleMenuAction::Start : TitleMenuAction::Exit;
        }
        return TitleMenuAction::None;
    }
}
