#pragma once
#include <SceneRuntime/Scene/SceneLayout.h>
#include <map>

namespace SceneRuntime
{
    struct PhysicsFrame;
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

    private:
        friend class PhysicsWorld;
        /// <summary>物理専用表現の数値姿勢と状態だけに、箱判定のプレイヤー移動を適用します。</summary>
        static bool AdvanceFrame(PhysicsFrame& frame, States& states, double seconds, float horizontal, float vertical,
                                 bool jump);
    };
}
