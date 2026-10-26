#pragma once
#include <SceneRuntime/Scene/EntityHandle.h>
#include <SceneRuntime/Scene/SceneChanges.h>
#include <vector>

namespace SceneRuntime
{
    struct RuntimePhysicalPose
    {
        EntityHandle entity, parent;
        ComponentInstanceHandle body, controller;
        LocalTransform transform;
    };
    struct RuntimePhysicalHistory
    {
        std::vector<RuntimePhysicalPose> previous, current;
    };
}
