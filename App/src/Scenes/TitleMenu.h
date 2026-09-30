#pragma once
#include "GameSettings.h"

namespace App
{
    enum class TitleMenuCue { None, Select, Confirm, Back, Error };
    enum class TitleMenuItem { Start, Settings, Exit };
    enum class TitleMenuAction { None, Start, Exit, SaveSettings };
    enum TitleMenuButton : unsigned int { MenuUp = 1, MenuDown = 2, MenuConfirm = 4, MenuLeft = 8, MenuRight = 16, MenuBack = 32 };

    struct TitleMenuInput
    {
        bool active = false;
        bool gamepadConnected = false;
        unsigned int keyboardButtons = 0;
        unsigned int gamepadButtons = 0;
        float stickY = 0.0f;
        float stickX = 0.0f;
    };

    class TitleMenu final
    {
    public:
        /// <summary>初回だけ登場演出を有効にします。</summary>
        explicit TitleMenu(bool playIntro = false) : introSeconds_(playIntro ? 0.0f : 0.65f) {}
        /// <summary>登場演出の進行率を返します。</summary>
        float IntroProgress() const { return introSeconds_ / 0.65f; }
        /// <summary>選択時の強調量を返します。</summary>
        float SelectionPulse() const { return selectionSeconds_ / 0.16f; }
        /// <summary>画面遷移の進行率を返します。</summary>
        float TransitionProgress() const { return transitionSeconds_ / 0.32f; }
        /// <summary>
        /// 入力の立ち上がりで選択・決定します。初回・復帰・接続直後の押下は抑止します。
        /// </summary>
        TitleMenuAction Update(const TitleMenuInput& input, double deltaSeconds = 0.0);
        /// <summary>
        /// 現在の選択項目を返します。
        /// </summary>
        TitleMenuItem GetSelected() const { return selected_; }
        /// <summary>
        /// 最後に操作した入力がゲームパッドかを返します。
        /// </summary>
        bool UsesGamepad() const { return usesGamepad_; }
        /// <summary>設定画面を表示中か返します。</summary>
        bool IsSettingsOpen() const { return settingsOpen_; }
        /// <summary>設定画面の選択行を返します。</summary>
        int GetSettingsRow() const { return settingsRow_; }
        /// <summary>編集中の設定を返します。</summary>
        const GameSettings& GetSettings() const { return draft_; }
        /// <summary>この更新で発生した操作音を返します。</summary>
        TitleMenuCue GetCue() const { return cue_; }
        /// <summary>保存済み設定を読み込みます。</summary>
        void LoadSettings(const GameSettings& settings) { saved_ = draft_ = settings; }
        /// <summary>保存成功時だけ設定画面を閉じます。</summary>
        void CompleteSave(bool success) { cue_ = success ? TitleMenuCue::Confirm : TitleMenuCue::Error; saveFailed_ = !success; if (success) { saved_ = draft_; settingsOpen_ = false; } }
        /// <summary>保存に失敗したか返します。</summary>
        bool SaveFailed() const { return saveFailed_; }
    private:
        unsigned int ReadPressedButtons(const TitleMenuInput& input);
        TitleMenuAction UpdateSettings(unsigned int pressed);
        TitleMenuAction UpdateMainMenu(unsigned int pressed);
        TitleMenuCue cue_ = TitleMenuCue::None;
        float introSeconds_ = 0.65f;
        float selectionSeconds_ = 0.0f;
        float transitionSeconds_ = 0.0f;
        TitleMenuAction pending_ = TitleMenuAction::None;
        bool transitionEmitted_ = false;
        GameSettings saved_;
        GameSettings draft_;
        int settingsRow_ = 0;
        bool settingsOpen_ = false;
        bool saveFailed_ = false;
        unsigned int stickXPrevious_ = 0;
        TitleMenuItem selected_ = TitleMenuItem::Start;
        unsigned int keyboardPrevious_ = 0;
        unsigned int gamepadPrevious_ = 0;
        unsigned int stickPrevious_ = 0;
        bool ready_ = false;
        bool connectedPrevious_ = false;
        bool usesGamepad_ = false;
        bool finished_ = false;
    };
}
