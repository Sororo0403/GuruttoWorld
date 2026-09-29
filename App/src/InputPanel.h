#pragma once

#if defined(_DEBUG) || defined(ENGINE_DEVELOPMENT)
#include <Engine/Input/Gamepad.h>

namespace App
{
    class InputPanel final
    {
    public:
        /// <summary>
        /// コントローラーの接続状態、アナログ入力、時間指定の振動テストを表示します。
        /// </summary>
        static void Draw(Engine::Gamepad& gamepad);
    };
}
#endif
