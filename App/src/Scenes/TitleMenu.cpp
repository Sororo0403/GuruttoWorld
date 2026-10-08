#include "TitleMenu.h"
#include <cmath>
#include <algorithm>
#include <optional>
#include <charconv>

namespace
{
    std::optional<int> UiRow(std::string_view event,std::string_view prefix)
    {
        if(!event.starts_with(prefix)) return std::nullopt;
        int row=0;
        const auto digits=event.substr(prefix.size());
        const auto result=std::from_chars(digits.data(),digits.data()+digits.size(),row);
        if(result.ec!=std::errc{} || result.ptr!=digits.data()+digits.size() || row<0 || row>=128) return std::nullopt;
        return row;
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
                if(const auto* entry=SelectedEntry()) pendingTarget_=entry->target;
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
            cue_=TitleMenuCue::Confirm; finished_=true; pending_=TitleMenuAction::Start; if(const auto* entry=SelectedEntry()) pendingTarget_=entry->target; return TitleMenuAction::None;
        }
        if(event=="back" && settingsOpen_) {
            draft_=saved_; settingsOpen_=false; saveFailed_=false; cue_=TitleMenuCue::Back; return TitleMenuAction::None;
        }
        if(event=="volume" && settingsOpen_) {
            const auto found=std::find_if(configuration_.settings.begin(),configuration_.settings.end(),[](const auto& s){return s.key=="volume";});
            if(found!=configuration_.settings.end()) {settingsRow_=static_cast<int>(found-configuration_.settings.begin()); ChangeSetting(1,true);}
            return TitleMenuAction::None;
        }
        if(const auto row=UiRow(event,"menu:"); row && !settingsOpen_ &&
            std::find(items_.begin(),items_.end(),static_cast<TitleMenuItem>(*row))!=items_.end()) {
            selected_=static_cast<TitleMenuItem>(*row); selectionSeconds_=selectionDuration_; return UpdateMainMenu(MenuConfirm);
        }
        if(const auto row=UiRow(event,"settings:"); row && settingsOpen_ && static_cast<size_t>(*row)<configuration_.settings.size()) {
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
            if(static_cast<int>(item)>=0 && static_cast<int>(item)<128 &&
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
            const int count=static_cast<int>(configuration_.settings.size());
            const int step=direction==MenuDown ? 1 : count-1;
            if (settingsOpen_) { if(count) settingsRow_=(settingsRow_+step)%count; }
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

    void TitleMenu::Configure(SceneRuntime::MenuConfiguration configuration) {
        SceneRuntime::ValidateMenu(configuration);
        configuration_=std::move(configuration); pressAnyButton_=configuration_.pressAnyButton;
        std::vector<TitleMenuItem> items;
        for(const auto& entry:configuration_.entries) items.push_back(static_cast<TitleMenuItem>(entry.index));
        SetItems(std::move(items));
        GameSettings defaults;
        for(const auto& row:configuration_.settings) if(row.action!="save") defaults.values[row.key]=row.initial;
        LoadSettings(defaults);
    }
    const SceneRuntime::MenuEntry* TitleMenu::SelectedEntry() const {
        const auto found=std::find_if(configuration_.entries.begin(),configuration_.entries.end(),[&](const auto& e){return e.index==static_cast<int>(selected_);});
        return found==configuration_.entries.end()?nullptr:&*found;
    }
    void TitleMenu::LoadSettings(const GameSettings& settings) {
        auto values=settings;
        for(const auto& row:configuration_.settings) if(row.action!="save") {
            const auto found=values.values.find(row.key);
            const float fallback=row.key=="volume" ? row.minimum+(row.maximum-row.minimum)*settings.Gain() : row.key=="motion" ? (settings.backgroundMotion?row.maximum:row.minimum) : row.initial;
            const float value=found==values.values.end() ? fallback : found->second;
            values.values[row.key]=std::isfinite(value)?std::clamp(value,row.minimum,row.maximum):row.initial;
            if(row.key=="volume") {values.volumeGain=(values.values[row.key]-row.minimum)/(row.maximum-row.minimum); values.volume=static_cast<int>(std::round(values.volumeGain*10));}
            if(row.key=="motion") values.backgroundMotion=values.values[row.key]!=row.minimum;
        }
        saved_=draft_=std::move(values);
    }
    void TitleMenu::ChangeSetting(int direction,bool confirm) {
        if(settingsRow_<0 || static_cast<size_t>(settingsRow_)>=configuration_.settings.size()) return;
        const auto& row=configuration_.settings[settingsRow_];
        if(row.action=="save") return;
        auto& value=draft_.values[row.key]; const float previous=value;
        if(row.action=="toggle") value=confirm ? (value==row.minimum?row.maximum:row.minimum) : direction>0?row.maximum:row.minimum;
        else value=confirm && value+row.step>row.maximum ? row.minimum : std::clamp(value+direction*row.step,row.minimum,row.maximum);
        if(row.key=="volume") {draft_.volumeGain=(value-row.minimum)/(row.maximum-row.minimum); draft_.volume=static_cast<int>(std::round(draft_.volumeGain*10));}
        if(row.key=="motion") draft_.backgroundMotion=value!=row.minimum;
        if(value!=previous) cue_=confirm?TitleMenuCue::Confirm:TitleMenuCue::Select;
    }
    TitleMenuAction TitleMenu::UpdateSettings(unsigned int pressed) {
        if(configuration_.settings.empty()) return TitleMenuAction::None;
        const auto horizontal=pressed&(MenuLeft|MenuRight);
        if(horizontal) {
            if(horizontal!=(MenuLeft|MenuRight)) ChangeSetting(horizontal==MenuRight?1:-1,false);
            return TitleMenuAction::None;
        }
        if(pressed&MenuConfirm) {
            if(configuration_.settings[settingsRow_].action=="save") return TitleMenuAction::SaveSettings;
            ChangeSetting(1,true);
        }
        return TitleMenuAction::None;
    }
    TitleMenuAction TitleMenu::UpdateMainMenu(unsigned int pressed) {
        if(!(pressed&MenuConfirm) || items_.empty()) return TitleMenuAction::None;
        const auto* entry=SelectedEntry(); if(!entry) return TitleMenuAction::None;
        cue_=TitleMenuCue::Confirm;
        if(entry->action=="settings") {
            settingsOpen_=true; settingsRow_=0; draft_=saved_; saveFailed_=false;
        } else if(entry->action=="setState") assignments_=entry->target;
        else {
            finished_=true; pendingTarget_=entry->target;
            pending_=entry->action=="quit"?TitleMenuAction::Exit:TitleMenuAction::Start;
            selectionSeconds_=selectionDuration_;
        }
        return TitleMenuAction::None;
    }
}
