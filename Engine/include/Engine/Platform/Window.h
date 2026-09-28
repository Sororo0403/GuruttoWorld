#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace Engine
{
    class Window final
    {
    public:
        /// <summary>
        /// メインウィンドウを管理するオブジェクトを初期化します。
        /// </summary>
        Window() = default;

        /// <summary>
        /// ウィンドウを破棄し、登録したウィンドウクラスを解除します。
        /// </summary>
        ~Window();

        /// <summary>
        /// ウィンドウの所有権を共有しないようにコピー生成を禁止します。
        /// </summary>
        Window(const Window&) = delete;

        /// <summary>
        /// ウィンドウの所有権を共有しないようにコピー代入を禁止します。
        /// </summary>
        Window& operator=(const Window&) = delete;

        /// <summary>
        /// メインスレッドにウィンドウを作成します。同時に作成できるメインウィンドウは一つです。
        /// </summary>
        /// <param name="title">タイトルバーに表示する文字列。</param>
        /// <param name="width">クライアント領域の幅。正のピクセル数を指定します。</param>
        /// <param name="height">クライアント領域の高さ。正のピクセル数を指定します。</param>
        /// <returns>作成に成功した場合は true、失敗した場合は false。</returns>
        bool Create(const wchar_t* title, int width = 1280, int height = 720);

        /// <summary>
        /// 作成済みのウィンドウを表示します。作成したスレッドから呼び出してください。
        /// </summary>
        void Show();

        /// <summary>
        /// 終了までウィンドウメッセージを処理します。作成したスレッドから呼び出してください。
        /// </summary>
        /// <returns>終了メッセージの終了コード。ウィンドウが未作成、または処理に失敗した場合は 1。</returns>
        int RunMessageLoop();

        /// <summary>
        /// 描画処理などに渡すためのウィンドウハンドルを取得します。
        /// </summary>
        /// <returns>作成済みのハンドル。未作成または破棄済みの場合は nullptr。</returns>
        HWND GetHandle() const noexcept;

    private:
        /// <summary>
        /// ウィンドウへのメッセージを処理し、破棄時に終了メッセージを送信します。
        /// </summary>
        /// <param name="handle">メッセージを受け取るウィンドウ。</param>
        /// <param name="message">メッセージの識別子。</param>
        /// <param name="wParam">メッセージに付随する情報。</param>
        /// <param name="lParam">メッセージに付随する情報。</param>
        /// <returns>メッセージの処理結果。</returns>
        static LRESULT CALLBACK WindowProcedure(HWND handle, UINT message, WPARAM wParam, LPARAM lParam);

        HINSTANCE instance_ = nullptr;
        HWND handle_ = nullptr;
        ATOM classAtom_ = 0;
    };
}
