#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <Windows.h>
#include <dinput.h>
#include <wrl/client.h>
#include <array>

namespace Engine
{
    class Keyboard final
    {
    public:
        /// <summary>
        /// 未初期化のキーボードを生成します。操作と破棄はウィンドウのスレッドで行ってください。
        /// </summary>
        Keyboard() = default;

        /// <summary>
        /// デバイスの取得を解除し、DirectInput を解放します。
        /// </summary>
        ~Keyboard();

        /// <summary>
        /// 入力デバイスのコピー生成を禁止します。
        /// </summary>
        Keyboard(const Keyboard&) = delete;

        /// <summary>
        /// 入力デバイスのコピー代入を禁止します。
        /// </summary>
        Keyboard& operator=(const Keyboard&) = delete;

        /// <summary>
        /// 指定したウィンドウで、非排他的なフォアグラウンド入力を初期化します。
        /// </summary>
        bool Initialize(HWND window);

        /// <summary>
        /// 入力を解除し、状態とリソースを初期化します。ウィンドウの破棄前に呼び出します。
        /// </summary>
        void Shutdown();

        /// <summary>
        /// メッセージ処理後に一度呼び出して状態を更新します。取得失敗時はクリアし、次フレームに再試行します。
        /// 再取得したフレームは押下・解放イベントを発生させません。フレーム間の短い入力は検出できない場合があります。
        /// </summary>
        void Update();

        /// <summary>
        /// 入力が取得できているか返します。非アクティブ時は false です。
        /// </summary>
        bool IsActive() const noexcept;
        HWND WindowHandle() const noexcept { return window_; }

        /// <summary>
        /// DIK_* で指定したキーを押しているか返します。範囲外は false です。
        /// </summary>
        bool IsDown(unsigned int key) const noexcept;

        /// <summary>
        /// DIK_* で指定したキーをこのフレームで押したか返します。
        /// </summary>
        bool IsPressed(unsigned int key) const noexcept;

        /// <summary>
        /// DIK_* で指定したキーをこのフレームで離したか返します。
        /// </summary>
        bool IsReleased(unsigned int key) const noexcept;

    private:
        /// <summary>
        /// 前回と今回の入力をクリアし、次回取得時の差分判定を抑止します。
        /// </summary>
        void ClearState() noexcept;

        Microsoft::WRL::ComPtr<IDirectInput8W> input_;
        Microsoft::WRL::ComPtr<IDirectInputDevice8W> device_;
        HWND window_ = nullptr;
        std::array<BYTE, 256> current_{};
        std::array<BYTE, 256> previous_{};
        bool active_ = false;
    };
}
