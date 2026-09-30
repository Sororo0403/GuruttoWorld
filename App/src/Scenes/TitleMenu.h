#pragma once
#include "GameSettings.h"

namespace App
{
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
        /// <summary>
        /// 入力の立ち上がりで選択・決定します。初回・復帰・接続直後の押下は抑止します。
        /// </summary>
        TitleMenuAction Update(const TitleMenuInput& input);
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
        /// <summary>保存済み設定を読み込みます。</summary>
        void LoadSettings(const GameSettings& settings) { saved_ = draft_ = settings; }
        /// <summary>保存成功時だけ設定画面を閉じます。</summary>
        void CompleteSave(bool success) { saveFailed_ = !success; if (success) { saved_ = draft_; settingsOpen_ = false; } }
        /// <summary>保存に失敗したか返します。</summary>
        bool SaveFailed() const { return saveFailed_; }
    private:
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
