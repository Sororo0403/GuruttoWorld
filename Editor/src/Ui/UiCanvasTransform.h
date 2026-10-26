#pragma once
#include <SceneRuntime/Ui/SceneUi.h>
#include <algorithm>
#include <cmath>

namespace Editor
{
    struct UiCanvasTransform
    {
        /// <summary>表示サイズを変更します。</summary>
        static SceneRuntime::RectTransformComponent Resize(const SceneRuntime::RectTransformComponent& start,
                                                           float parentRotation, float scale,
                                                           const std::array<float, 2>& handle,
                                                           const std::array<float, 2>& delta);
        /// <summary>操作の基準位置を求めます。</summary>
        static SceneRuntime::RectTransformComponent Pivot(const SceneRuntime::RectTransformComponent& start,
                                                          const SceneRuntime::UiRect& rect,
                                                          const std::array<float, 2>& delta);
    };
}
