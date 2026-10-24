#pragma once
#include <SceneRuntime/Scripts/ScriptRuntime.h>
#include <SceneRuntime/Physics/PhysicsWorld.h>

namespace SceneRuntime
{
    // 編集ドキュメントと描画投影から分離し、実行ワールドだけが所有します。
    struct SceneExecutionState final
    {
        struct PhysicalPose
        {
            std::array<float, 3> position{}, rotation{}, scale{1, 1, 1};
            std::string parent;
        };
        struct Values
        {
            ScriptRuntime scripts;
            ScenePhysics::States physics;
            std::map<std::string, float> inputValues;
            std::map<std::string, bool> inputPressed, fixedPressed;
            std::map<std::string, PhysicalPose> previousPhysical, currentPhysical;
            double fixedSeconds = 0;
            bool fixedJump = false, frameUpdating = false;
        };
        Values values;
        PhysicsWorld physicsWorld;
    };
}
