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
        static bool AnimationChanged(const ScenePlacement& before, const ScenePlacement& after)
        {
            return std::tie(before.animator, before.terrain, before.tilemap) !=
                       std::tie(after.animator, after.terrain, after.tilemap) ||
                   (before.animator && MeshChanged(before, after));
        }
        /// <summary>UIテクスチャの交換や破棄が必要になる変更を判定します。</summary>
        static bool UiChanged(const ScenePlacement& before, const ScenePlacement& after)
        {
            if (before.inputField != after.inputField || before.slider.has_value() != after.slider.has_value() ||
                before.toggle.has_value() != after.toggle.has_value() ||
                before.image.has_value() != after.image.has_value() ||
                before.text.has_value() != after.text.has_value())
                return true;
            if (before.image && before.image->texture != after.image->texture)
                return true;
            if (before.text)
            {
                if (before.text->text != after.text->text || before.text->font != after.text->font ||
                    before.text->fontSize != after.text->fontSize)
                    return true;
                const std::array<float, 2> defaultSize{200, 60};
                const auto oldSize = before.rectTransform ? before.rectTransform->size : defaultSize;
                const auto newSize = after.rectTransform ? after.rectTransform->size : defaultSize;
                if (oldSize != newSize)
                    return true;
            }
            return false;
        }
        /// <summary>既存の描画資源を交換し得る変更を、描画側の規則で判定します。</summary>
        static bool RequiresSynchronization(const ScenePlacement& before, const ScenePlacement& after)
        {
            return MeshChanged(before, after) || AnimationChanged(before, after) || UiChanged(before, after);
        }
    };
}
