#pragma once
#include <Engine/Core/Validation.h>
#include <SceneRuntime/Rendering/SceneViewQueries.h>
#include <Engine/Graphics/Camera.h>
#include <algorithm>
#include <optional>
#include <cmath>

namespace SceneRuntime
{
    // エディター表示ではルート Canvas を中心とし、ワールドの 1 単位を UI の 100 ピクセルとして扱います。
    struct SceneCanvas
    {
        /// <summary>処理対象のルートを求めます。</summary>
        static const ScenePlacement* Root(const SceneLayout& layout, const ScenePlacement& object);
        /// <summary>配置から変換行列を求めます。</summary>
        static std::optional<DirectX::XMFLOAT4X4> Matrix(const SceneViewQueries& view, const ScenePlacement& object);
        /// <summary>指定した形状との交差を求めます。</summary>
        static std::optional<std::array<float, 2>> Intersect(const DirectX::XMFLOAT4X4& matrix,
                                                             const Engine::Camera& camera, float ndcX, float ndcY);
    };
}
