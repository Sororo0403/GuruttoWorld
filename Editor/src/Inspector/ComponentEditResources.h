#pragma once
#include "../Editing/EditState.h"
#include <SceneRuntime/Rendering/SceneResourceChanges.h>

namespace Editor
{
    struct ComponentEditResources
    {
        /// <summary>資源交換の判定を、描画準備を所有するランタイムへ委譲します。</summary>
        static bool Changed(const SceneRuntime::ScenePlacement& before, const SceneRuntime::ScenePlacement& after)
        {
            return SceneRuntime::SceneResourceChanges::RequiresSynchronization(before, after);
        }
        /// <summary>処理の前にGPUの待機が必要かを確認します。</summary>
        static bool NeedsIdle(const SceneRuntime::SceneLayout& layout, const ObjectRequest& request);
    };
}
