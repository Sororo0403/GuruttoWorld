#include "TitleMenu.h"
#include <cmath>

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
        const unsigned int pad = input.gamepadConnected ? input.gamepadButtons : 0;
        const unsigned int keyboardPressed = ready_ ? input.keyboardButtons & ~keyboardPrevious_ : 0;
        unsigned int padPressed = 0;
        if (ready_ && input.gamepadConnected && connectedPrevious_)
        {
            padPressed = pad & ~gamepadPrevious_;
            if (stick != stickPrevious_) padPressed |= stick;
        }
        keyboardPrevious_ = input.keyboardButtons;
        gamepadPrevious_ = pad;
        stickPrevious_ = stick;
        connectedPrevious_ = input.gamepadConnected;
        ready_ = true;
        if (!input.gamepadConnected || keyboardPressed != 0) usesGamepad_ = false;
        else if (padPressed != 0) usesGamepad_ = true;
        if (finished_) return TitleMenuAction::None;
        const unsigned int pressed = keyboardPressed | padPressed;
        const unsigned int direction = pressed & (MenuUp | MenuDown);
        if (direction != 0)
        {
            if (direction != (MenuUp | MenuDown))
                selected_ = selected_ == TitleMenuItem::Start ? TitleMenuItem::Exit : TitleMenuItem::Start;
            // 選択移動と同じフレームの決定は無視し、意図せぬ終了を防ぎます。
            return TitleMenuAction::None;
        }
        if ((pressed & MenuConfirm) != 0)
        {
            finished_ = true;
            return selected_ == TitleMenuItem::Start ? TitleMenuAction::Start : TitleMenuAction::Exit;
        }
        return TitleMenuAction::None;
    }
}
