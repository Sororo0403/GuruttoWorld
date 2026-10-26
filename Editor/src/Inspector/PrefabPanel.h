#pragma once
#include "../Editing/EditState.h"
#include <SceneRuntime/Scene/Prefab.h>
#include <imgui.h>

namespace Editor
{
    /// <summary>Prefabの外部接続と旧形式からの移行操作を表示します。</summary>
    void DrawPrefabBindings(EditState& state, const SceneRuntime::SceneLayout& scene,
                            const SceneRuntime::ScenePlacement& object);
    /// <summary>Prefabの変更箇所の編集画面を描画します。</summary>
    void DrawPrefabOverrides(EditState& state, const SceneRuntime::SceneLayout& scene,
                             const SceneRuntime::ScenePlacement& object);
}
