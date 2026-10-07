#pragma once
#include <SceneRuntime/SceneLayout.h>
#include <map>

namespace SceneRuntime
{
    // Kinematic controllers versus conservative world-axis boxes; no rigid-body impulses.
    class ScenePhysics final
    {
    public:
        struct Body { float verticalSpeed=0; bool grounded=false; };
        using States=std::map<std::string,Body>;
        // Call with copies and commit only on success. Inputs are local to the controller parent.
        static bool Advance(SceneLayout& layout, States& states, double seconds,
            float horizontal, float vertical, bool jump);
    };
}
