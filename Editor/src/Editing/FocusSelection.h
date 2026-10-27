#pragma once
#include <Engine/Graphics/Camera.h>
#include <SceneRuntime/Rendering/SceneViewQueries.h>
#include <SceneRuntime/Ui/SceneCanvas.h>
#include <SceneRuntime/Ui/SceneUi.h>
#include <span>
#include <vector>
#include <optional>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace Editor
{
    /// <summary>
    /// 現在の視線方向を保ち、変換後の境界を囲む球が収まるように調整します。
    /// </summary>
    std::optional<std::array<float, 3>> FocusPosition(std::span<const std::array<float, 3>> corners,
                                                      const Engine::Camera& camera);
    /// <summary>選択対象へ視点を合わせる位置を求めます。</summary>
    std::optional<std::array<float, 3>> FocusPosition(const SceneRuntime::SceneViewQueries& view,
                                                      const std::vector<std::string>& ids,
                                                      const Engine::Camera& camera);
}
