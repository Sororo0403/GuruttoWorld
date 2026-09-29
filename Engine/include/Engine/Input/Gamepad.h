#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <Xinput.h>
#include <array>
#include <chrono>

namespace Engine
{
    class Gamepad final
    {
    public:
        /// <summary>
        /// XInput のユーザー番号（0～3）を指定します。範囲外は未接続として扱います。
        /// 操作と破棄は同じスレッドで行い、同じ番号の管理オブジェクトは一つにしてください。
        /// </summary>
        explicit Gamepad(unsigned int userIndex = 0) noexcept;

        /// <summary>
        /// このオブジェクトが開始した振動を停止します。
        /// </summary>
        ~Gamepad();

        /// <summary>
        /// 振動制御のコピー生成を禁止します。
        /// </summary>
        Gamepad(const Gamepad&) = delete;

        /// <summary>
        /// 振動制御のコピー代入を禁止します。
        /// </summary>
        Gamepad& operator=(const Gamepad&) = delete;

        /// <summary>
        /// フレームごとに入力と振動期限を更新します。非アクティブ時は入力をクリアして振動を止めます。
        /// 接続直後・復帰直後の押下と解放は抑止します。切断時にも入力をクリアします。
        /// </summary>
        void Update(bool active);

        /// <summary>
        /// 最後の更新で接続を確認できたか返します。
        /// </summary>
        bool IsConnected() const noexcept;

        /// <summary>
        /// XINPUT_GAMEPAD_* のマスクで指定したボタンをすべて押しているか返します。
        /// </summary>
        bool IsDown(WORD buttons) const noexcept;

        /// <summary>
        /// 指定したボタンの組み合わせがこのフレームで押下状態になったか返します。
        /// </summary>
        bool IsPressed(WORD buttons) const noexcept;

        /// <summary>
        /// 指定したボタンの組み合わせがこのフレームで押下状態でなくなったか返します。
        /// </summary>
        bool IsReleased(WORD buttons) const noexcept;

        /// <summary>
        /// 円形デッドゾーン適用後の左スティックを返します。長さは 0～1、右と上が正です。
        /// </summary>
        std::array<float, 2> GetLeftStick() const noexcept;

        /// <summary>
        /// 円形デッドゾーン適用後の右スティックを返します。長さは 0～1、右と上が正です。
        /// </summary>
        std::array<float, 2> GetRightStick() const noexcept;

        /// <summary>
        /// しきい値適用後の左トリガーを 0～1 で返します。
        /// </summary>
        float GetLeftTrigger() const noexcept;

        /// <summary>
        /// しきい値適用後の右トリガーを 0～1 で返します。
        /// </summary>
        float GetRightTrigger() const noexcept;

        /// <summary>
        /// 左右の強さ（0～1）と正の秒数（最大 60 秒）を指定して振動します。範囲外や未接続時は false です。
        /// 期限の停止には Update の継続呼び出しが必要です。経過時間には実時間を使用します。
        /// </summary>
        bool Vibrate(float left, float right, float seconds);

        /// <summary>
        /// 開始した振動を停止します。失敗時は次の Update で再試行します。
        /// </summary>
        void StopVibration();

    private:
        unsigned int userIndex_;
        XINPUT_GAMEPAD current_{};
        WORD previousButtons_ = 0;
        bool connected_ = false;
        bool active_ = false;
        bool vibrating_ = false;
        std::chrono::steady_clock::time_point vibrationEnd_{};
    };
}
