#pragma once
#include <SceneRuntime/Scene/SceneLayout.h>

namespace SceneRuntime
{
    class SceneResourceChanges final
    {
    public:
        /// <summary>メッシュまたはマテリアルの準備内容が変わるかを判定します。</summary>
        static bool MeshChanged(const ScenePlacement& before, const ScenePlacement& after)
        {
            return std::tie(before.meshRenderer, before.material) != std::tie(after.meshRenderer, after.material);
        }
        /// <summary>アニメーションと手続き形状の準備内容が変わるかを判定します。</summary>
        static bool AnimationChanged(const ScenePlacement& before, const ScenePlacement& after);
        /// <summary>UIテクスチャの交換や破棄が必要になる変更を判定します。</summary>
        static bool UiChanged(const ScenePlacement& before, const ScenePlacement& after);
        /// <summary>既存の描画資源を交換し得る変更を、描画側の規則で判定します。</summary>
        static bool RequiresSynchronization(const ScenePlacement& before, const ScenePlacement& after)
        {
            return MeshChanged(before, after) || AnimationChanged(before, after) || UiChanged(before, after);
        }
    };
}
