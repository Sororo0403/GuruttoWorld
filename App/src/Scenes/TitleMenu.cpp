#include "TitleMenu.h"
#include <cmath>
#include <algorithm>
#include <optional>

namespace
{
    std::optional<int> UiRow(std::string_view event,std::string_view prefix)
    {
        if(event.size()!=prefix.size()+1 || !event.starts_with(prefix)) return std::nullopt;
        const char row=event.back();
        if(row<'0' || row>'2') return std::nullopt;
        return row-'0';
    }
    unsigned int ReadStick(bool connected, float value, unsigned int previous,
        unsigned int positive, unsigned int negative)
    {
        if (!connected || !std::isfinite(value) || std::abs(value) <= 0.3f) return 0;
        if (value >= 0.55f) return positive;
        if (value <= -0.55f) return negative;
        return previous;
    }
}

namespace App
{
    TitleMenuAction TitleMenu::Update(const TitleMenuInput& input, double deltaSeconds)
    {
        cue_ = TitleMenuCue::None;
        if (!input.active)
        {
            ready_ = false;
            return TitleMenuAction::None;
        }
        const bool entering = introSeconds_ < introDuration_;
        const float elapsed = std::isfinite(deltaSeconds) ? static_cast<float>(std::clamp(deltaSeconds, 0.0, 0.1)) : 0.0f;
        introSeconds_ = std::min(introDuration_, introSeconds_ + elapsed);
        selectionSeconds_ = std::max(0.0f, selectionSeconds_ - elapsed);
        // 覆い切ったフレームを描いてから次の更新で遷移を通知します。
        if (finished_)
        {
            return UpdateTransition(elapsed);
        }
        const bool wasReady = ready_;
        const unsigned int pressed = ReadPressedButtons(input);
        if (pressAnyButton_)
        {
            if (wasReady && input.anyButtonPressed)
            {
                introSeconds_ = introDuration_;
                cue_ = TitleMenuCue::Confirm;
                finished_ = true;
                pending_ = TitleMenuAction::Start;
            }
            return TitleMenuAction::None;
        }
        if (entering)
        {
            if (pressed != 0) introSeconds_ = introDuration_;
            return TitleMenuAction::None;
        }
        if (settingsOpen_ && (pressed & MenuBack))
        {
            cue_ = TitleMenuCue::Back;
            draft_ = saved_;
            settingsOpen_ = false;
            saveFailed_ = false;
            return TitleMenuAction::None;
        }
        const unsigned int direction = pressed & (MenuUp | MenuDown);
        if (direction != 0)
        {
            MoveSelection(direction);
            // 選択移動と同じフレームの決定は無視し、意図せぬ終了を防ぎます。
            return TitleMenuAction::None;
        }
        if (settingsOpen_) return UpdateSettings(pressed);
        return UpdateMainMenu(pressed);
    }

    TitleMenuAction TitleMenu::ActivateUi(std::string_view event)
    {
        if(finished_) return TitleMenuAction::None;
        if(introSeconds_<introDuration_) {introSeconds_=introDuration_; return TitleMenuAction::None;}
        if(pressAnyButton_ && event=="start") {
            cue_=TitleMenuCue::Confirm; finished_=true; pending_=TitleMenuAction::Start; return TitleMenuAction::None;
        }
        if(event=="back" && settingsOpen_) {
            draft_=saved_; settingsOpen_=false; saveFailed_=false; cue_=TitleMenuCue::Back; return TitleMenuAction::None;
        }
        if(event=="volume" && settingsOpen_) {draft_.volume=(draft_.volume+1)%11; cue_=TitleMenuCue::Select; return TitleMenuAction::None;}
        if(const auto row=UiRow(event,"menu:"); row && !settingsOpen_ &&
            std::find(items_.begin(),items_.end(),static_cast<TitleMenuItem>(*row))!=items_.end()) {
            selected_=static_cast<TitleMenuItem>(*row); selectionSeconds_=selectionDuration_; return UpdateMainMenu(MenuConfirm);
        }
        if(const auto row=UiRow(event,"settings:"); row && settingsOpen_) {
            settingsRow_=*row; selectionSeconds_=selectionDuration_; return UpdateSettings(MenuConfirm);
        }
        return TitleMenuAction::None;
    }

    void TitleMenu::SelectUi(TitleMenuItem item)
    {
        if (finished_ || settingsOpen_ || introSeconds_<introDuration_ ||
            std::find(items_.begin(),items_.end(),item)==items_.end() || selected_==item) return;
        selected_=item;
        selectionSeconds_=selectionDuration_;
        cue_=TitleMenuCue::Select;
    }

    TitleMenuAction TitleMenu::UpdateTransition(float elapsed)
    {
        if (transitionSeconds_ >= transitionDuration_ && !transitionEmitted_)
        {
            transitionEmitted_ = true;
            return pending_;
        }
        transitionSeconds_ = std::min(transitionDuration_, transitionSeconds_ + elapsed);
        return TitleMenuAction::None;
    }
    void TitleMenu::SetTransitionDuration(float seconds)
    {
        if (std::isfinite(seconds) && seconds>=0.1f && seconds<=10.0f && !finished_)
            transitionDuration_=seconds;
    }
    void TitleMenu::SetPresentationDurations(float intro,float selection)
    {
        if(finished_) return;
        const float progress=IntroProgress();
        if(std::isfinite(intro) && intro>=0.01f && intro<=10) introDuration_=intro;
        if(std::isfinite(selection) && selection>=0.01f && selection<=10) selectionDuration_=selection;
        introSeconds_=progress*introDuration_;
        selectionSeconds_=std::min(selectionSeconds_,selectionDuration_);
    }
    void TitleMenu::SetItems(std::vector<TitleMenuItem> items)
    {
        std::vector<TitleMenuItem> unique;
        for(const auto item:items)
            if(item>=TitleMenuItem::Start && item<=TitleMenuItem::Exit &&
                std::find(unique.begin(),unique.end(),item)==unique.end()) unique.push_back(item);
        items_=std::move(unique);
        if(!items_.empty() && std::find(items_.begin(),items_.end(),selected_)==items_.end()) selected_=items_.front();
    }

    void TitleMenu::MoveSelection(unsigned int direction)
    {
        if (direction != (MenuUp | MenuDown))
        {
            cue_ = TitleMenuCue::Select;
            selectionSeconds_ = selectionDuration_;
            const int step = direction == MenuDown ? 1 : 2;
            if (settingsOpen_) settingsRow_ = (settingsRow_ + step) % 3;
            else {
                if(items_.empty()) return;
                const auto current=std::find(items_.begin(),items_.end(),selected_);
                const auto index=static_cast<size_t>(current-items_.begin());
                const auto offset=direction==MenuDown ? size_t(1) : items_.size()-1;
                selected_=items_[(index+offset)%items_.size()];
            }
        }
    }

    unsigned int TitleMenu::ReadPressedButtons(const TitleMenuInput& input)
    {
        const unsigned int stick = ReadStick(input.gamepadConnected, input.stickY, stickPrevious_, MenuUp, MenuDown);
        const unsigned int stickX = ReadStick(input.gamepadConnected, input.stickX, stickXPrevious_, MenuRight, MenuLeft);
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
        return keyboardPressed | padPressed;
    }

    TitleMenuAction TitleMenu::UpdateSettings(unsigned int pressed)
    {
        const unsigned int horizontal = pressed & (MenuLeft | MenuRight);
        if (horizontal != 0)
        {
            if (horizontal != (MenuLeft | MenuRight))
            {
                const auto previous = draft_;
                if (settingsRow_ == 0) draft_.volume = std::clamp(draft_.volume + (horizontal == MenuRight ? 1 : -1), 0, 10);
                if (settingsRow_ == 1) draft_.backgroundMotion = horizontal == MenuRight;
                if (previous.volume != draft_.volume || previous.backgroundMotion != draft_.backgroundMotion)
                    cue_ = TitleMenuCue::Select;
            }
            return TitleMenuAction::None;
        }
        if (pressed & MenuConfirm)
        {
            if (settingsRow_ == 1) { draft_.backgroundMotion = !draft_.backgroundMotion; cue_ = TitleMenuCue::Confirm; }
            if (settingsRow_ == 2) return TitleMenuAction::SaveSettings;
        }
        return TitleMenuAction::None;
    }

    TitleMenuAction TitleMenu::UpdateMainMenu(unsigned int pressed)
    {
        if ((pressed & MenuConfirm) != 0 && !items_.empty())
        {
            cue_ = TitleMenuCue::Confirm;
            if (selected_ == TitleMenuItem::Settings)
            {
                settingsOpen_ = true;
                settingsRow_ = 0;
                draft_ = saved_;
                saveFailed_ = false;
                return TitleMenuAction::None;
            }
            finished_ = true;
            pending_ = selected_ == TitleMenuItem::Start ? TitleMenuAction::Start : TitleMenuAction::Exit;
            selectionSeconds_ = selectionDuration_;
            return TitleMenuAction::None;
        }
        return TitleMenuAction::None;
    }
}
