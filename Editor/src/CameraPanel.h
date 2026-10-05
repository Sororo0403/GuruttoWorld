#pragma once

#if defined(_DEBUG) || defined(ENGINE_DEVELOPMENT)
#include <Engine/DevTools/DebugCamera.h>
#include <Engine/Input/Keyboard.h>

namespace Editor
{
    class CameraPanel final
    {
    public:
        /// <summary>
        /// カメラの操作パネルを生成します。
        /// </summary>
        CameraPanel() = default;

        /// <summary>
        /// ImGui のフレーム開始後、シーン描画前に呼び出し、入力に応じたカメラ更新と設定画面の表示を行います。
        /// </summary>
        void Draw(Engine::DebugCamera& camera, const Engine::Keyboard& keyboard, double deltaSeconds, bool allowMovement = true);

        /// <summary>
        /// フォーカス喪失などで中断したドラッグ状態を解除します。
        /// </summary>
        void CancelDrag() noexcept;

    private:
        void ResetFromKeyboard(Engine::DebugCamera& camera, const Engine::Keyboard& keyboard);
        void UpdateInput(Engine::DebugCamera& camera, const Engine::Keyboard& keyboard, double deltaSeconds);
        bool dragging_ = false;
    };
}
#endif
