#pragma once
#include <SceneRuntime/Scene/SceneLayout.h>
#include <map>

namespace SceneRuntime
{
    // キネマティックコントローラーと、形状を包含するワールド軸に沿った箱との判定です。剛体への力積は適用しません。
    class ScenePhysics final
    {
    public:
        struct Body
        {
            float verticalSpeed = 0;
            bool grounded = false;
        };
        using States = std::map<std::string, Body>;
        /// <summary>
        /// コピーを渡して呼び出し、成功時だけ確定してください。入力はコントローラーの親を基準とするローカル値です。
        /// </summary>
        static bool Advance(SceneLayout& layout, States& states, double seconds, float horizontal, float vertical,
                            bool jump);
    };
}
