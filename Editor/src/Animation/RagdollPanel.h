#pragma once
#include "../Editing/EditState.h"
#include <imgui.h>
namespace Editor
{
    /// <summary>ラグドールの生成と物理・アニメーション合成をインスペクターで編集します。</summary>
    bool DrawRagdoll(EditState& state, SceneRuntime::ScenePlacement& object);
}
