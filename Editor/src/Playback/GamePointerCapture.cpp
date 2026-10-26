#include "GamePointerCapture.h"

namespace Editor
{
    auto GamePointerCapture::Update(HWND window, const SceneViewport& viewport, bool allowed) -> bool
    {
        if (!allowed || !window || GetForegroundWindow() != window)
        {
            Release();
            return false;
        }
        POINT points[2]{
            {static_cast<LONG>(viewport.x), static_cast<LONG>(viewport.y)},
            {static_cast<LONG>(viewport.x + viewport.width), static_cast<LONG>(viewport.y + viewport.height)}};
        MapWindowPoints(window, nullptr, points, 2);
        RECT client{};
        if (!GetClientRect(window, &client))
        {
            Release();
            return false;
        }
        MapWindowPoints(window, nullptr, reinterpret_cast<POINT*>(&client), 2);
        RECT bounds{points[0].x, points[0].y, points[1].x, points[1].y};
        if (!IntersectRect(&bounds, &bounds, &client))
        {
            Release();
            return false;
        }
        if (bounds.right <= bounds.left || bounds.bottom <= bounds.top)
        {
            Release();
            return false;
        }
        if (!captured_ && !GetClipCursor(&previous_))
            return false;
        if (!ClipCursor(&bounds))
        {
            Release();
            return false;
        }
        bounds_ = bounds;
        captured_ = true;
        return true;
    }

    auto GamePointerCapture::Release() -> void
    {
        if (!captured_)
            return;
        RECT current{};
        // フォーカス変更後に別のアプリが取得したキャプチャーを上書きしません。
        if (GetClipCursor(&current) && EqualRect(&current, &bounds_))
            ClipCursor(&previous_);
        captured_ = false;
    }
}
