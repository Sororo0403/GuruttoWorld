#pragma once
#include "../Editing/EditState.h"
#include <SceneRuntime/Ui/SceneUi.h>
#include <cmath>

namespace Editor
{
    struct UiLayoutTools
    {
        enum class Arrange
        {
            Left,
            CenterX,
            Right,
            Top,
            CenterY,
            Bottom,
            Horizontal,
            Vertical
        };
        /// <summary>選択中の対象を取得します。</summary>
        static std::vector<SceneRuntime::ScenePlacement> Selection(const SceneRuntime::SceneLayout& layout,
                                                                   const EditState& state);
        /// <summary>対象の位置を移動します。</summary>
        static void Translate(SceneRuntime::ScenePlacement& object, const SceneRuntime::UiRect& rect,
                              const std::array<float, 2>& delta);
        /// <summary>アンカーの座標を求めます。</summary>
        static std::optional<SceneRuntime::ScenePlacement> Anchor(const SceneRuntime::SceneLayout& layout,
                                                                  const SceneRuntime::ScenePlacement& object,
                                                                  unsigned int width, unsigned int height,
                                                                  const SceneRuntime::UiState& preview,
                                                                  const std::array<float, 2>& value);
        struct Bounds
        {
            float min, max, center;
            size_t index;
            SceneRuntime::UiRect rect;
        };
        /// <summary>配置対象全体の境界を求めます。</summary>
        static void ArrangeBounds(std::vector<SceneRuntime::ScenePlacement>& result, std::vector<Bounds>& bounds,
                                  Arrange action, size_t axis, float left, float right, bool distribute);
        /// <summary>変更内容を反映します。</summary>
        static std::vector<SceneRuntime::ScenePlacement> Apply(const SceneRuntime::SceneLayout& layout,
                                                               const EditState& state, unsigned int width,
                                                               unsigned int height,
                                                               const SceneRuntime::UiState& preview, Arrange action);
    };
}
