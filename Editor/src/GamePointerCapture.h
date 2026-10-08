#pragma once
#include "SceneViewport.h"
#include <Windows.h>

namespace Editor {
class GamePointerCapture final {
public:
    ~GamePointerCapture() {Release();}
    GamePointerCapture()=default;
    GamePointerCapture(const GamePointerCapture&)=delete;
    GamePointerCapture& operator=(const GamePointerCapture&)=delete;
    static bool Allowed(bool requested,bool playing,bool active,bool focused,bool blocked,const SceneViewport& viewport) {
        return requested && playing && active && focused && !blocked && viewport.Valid();
    }
    bool Update(HWND window,const SceneViewport& viewport,bool allowed) {
        if(!allowed || !window || GetForegroundWindow()!=window) {Release();return false;}
        POINT points[2]{{static_cast<LONG>(viewport.x),static_cast<LONG>(viewport.y)},
            {static_cast<LONG>(viewport.x+viewport.width),static_cast<LONG>(viewport.y+viewport.height)}};
        MapWindowPoints(window,nullptr,points,2);
        RECT client{};if(!GetClientRect(window,&client)) {Release();return false;}
        MapWindowPoints(window,nullptr,reinterpret_cast<POINT*>(&client),2);
        RECT bounds{points[0].x,points[0].y,points[1].x,points[1].y};
        if(!IntersectRect(&bounds,&bounds,&client)) {Release();return false;}
        if(bounds.right<=bounds.left || bounds.bottom<=bounds.top) {Release();return false;}
        if(!captured_ && !GetClipCursor(&previous_)) return false;
        if(!ClipCursor(&bounds)) {Release();return false;}
        bounds_=bounds;captured_=true;return true;
    }
    void Release() {
        if(!captured_) return;
        RECT current{};
        // Do not overwrite capture acquired by another app after a focus change.
        if(GetClipCursor(&current) && EqualRect(&current,&bounds_)) ClipCursor(&previous_);
        captured_=false;
    }
private:
    RECT previous_{},bounds_{};
    bool captured_=false;
};
}
