#pragma once
#include "ComponentWidgets.h"
#include "../Editing/EditState.h"
#include "../Project/ProjectCatalog.h"
#include "JointPanel.h"
#include <imgui.h>

namespace Editor
{
    /// <summary>剛体の編集画面を描画します。</summary>
    bool DrawRigidBody(EditState& state, SceneRuntime::ScenePlacement& candidate);
    /// <summary>衝突メッシュの編集画面を描画します。</summary>
    bool DrawColliderMesh(SceneRuntime::BoxColliderComponent& c, const ProjectCatalog* catalog);
    /// <summary>コライダーの編集画面を描画します。</summary>
    bool DrawCollider(EditState& state, SceneRuntime::ScenePlacement& candidate, const ProjectCatalog* catalog);
    /// <summary>物理設定の編集画面を描画します。</summary>
    bool DrawPhysics(EditState& state, SceneRuntime::ScenePlacement& candidate, const ProjectCatalog* catalog);
}
