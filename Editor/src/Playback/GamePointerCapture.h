#pragma once
#include "../Viewport/SceneViewport.h"
#include <Windows.h>

namespace Editor
{
    class GamePointerCapture final
    {
    public:
        ~GamePointerCapture()
        {
            Release();
        }
        GamePointerCapture() = default;
        GamePointerCapture(const GamePointerCapture&) = delete;
        GamePointerCapture& operator=(const GamePointerCapture&) = delete;
        static bool Allowed(bool requested, bool playing, bool active, bool focused, bool blocked,
                            const SceneViewport& viewport)
        {
            return requested && playing && active && focused && !blocked && viewport.Valid();
        }
        /// <summary>状態を更新します。</summary>
        bool Update(HWND window, const SceneViewport& viewport, bool allowed);
        /// <summary>保持する資源を解放します。</summary>
        void Release();

    private:
        RECT previous_{}, bounds_{};
        bool captured_ = false;
    };
}
