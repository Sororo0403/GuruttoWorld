#pragma once
#include "EditState.h"

namespace Editor
{
    class EnvironmentPanel final
    {
    public:
        static bool Draw(EditState& state, SceneRuntime::ScenePlacement& placement);
        static bool Add(SceneRuntime::ScenePlacement& placement);
        static std::string NewId(const SceneRuntime::ScenePlacement& placement, const std::string& base);
    };
}
