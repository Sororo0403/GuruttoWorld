#pragma once
#include "../Editing/EditState.h"
#include <imgui.h>
#include <cstring>
#include <functional>

namespace Editor
{
    /// <summary>種類に応じた距離または角度制限を編集します。</summary>
    bool DrawJointLimits(SceneRuntime::JointComponent& joint, const std::function<void()>& track);
    /// <summary>剛体接続を編集しUndo対象の操作を追跡します。</summary>
    bool DrawJoint(EditState& state, SceneRuntime::ScenePlacement& candidate);
}
