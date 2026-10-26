#pragma once
#include <Engine/Input/InputActions.h>
#include <imgui.h>

namespace Editor
{
    class InputBindingsPanel final
    {
    public:
        /// <summary>編集画面を描画します。</summary>
        static void Draw(Engine::InputActions::Bindings& bindings);

    private:
        /// <summary>キーの編集画面を描画します。</summary>
        static void DrawKeys(Engine::InputBinding& binding);

        /// <summary>ゲームパッドのボタンの編集画面を描画します。</summary>
        static void DrawPadButtons(Engine::InputBinding& binding);

        /// <summary>軸の編集画面を描画します。</summary>
        static void DrawAxis(Engine::InputBinding& binding);

        /// <summary>キーの編集画面を描画します。</summary>
        static void DrawKey(unsigned int& key);
    };
}
