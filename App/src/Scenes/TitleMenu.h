#pragma once

namespace App
{
    enum class TitleMenuItem { Start, Settings, Exit };
    enum class TitleMenuAction { None, Start, Exit };
    enum TitleMenuButton : unsigned int { MenuUp = 1, MenuDown = 2, MenuConfirm = 4 };

    struct TitleMenuInput
    {
        bool active = false;
        bool gamepadConnected = false;
        unsigned int keyboardButtons = 0;
        unsigned int gamepadButtons = 0;
        float stickY = 0.0f;
    };

    class TitleMenu final
    {
    public:
        /// <summary>
        /// 入力の立ち上がりで選択・決定します。初回・復帰・接続直後の押下は抑止します。
        /// </summary>
        TitleMenuAction Update(const TitleMenuInput& input);
        /// <summary>
        /// 現在の選択項目を返します。設定は未接続のため選択を飛ばします。
        /// </summary>
        TitleMenuItem GetSelected() const { return selected_; }
        /// <summary>
        /// 最後に操作した入力がゲームパッドかを返します。
        /// </summary>
        bool UsesGamepad() const { return usesGamepad_; }
    private:
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
