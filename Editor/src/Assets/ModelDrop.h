#pragma once
#include "../Viewport/SceneViewport.h"
#include <algorithm>
#include <Engine/Graphics/Camera.h>

namespace Editor
{
    inline constexpr const char* ModelPayload = "WP1_PROJECT_MODEL";

    /// <summary>
    /// エディターの地面に配置します。地平線や空へのドロップでは、レイに沿って 8 単位先の点を使います。
    /// </summary>
    std::optional<std::array<float, 3>> ModelDropPosition(const Engine::Camera& camera, const SceneViewport& viewport,
                                                          float x, float y);
}
