#pragma once
#include <SceneRuntime/Animation/Animator.h>
#include <SceneRuntime/Scene/SceneTransforms.h>
#include <SceneRuntime/Scene/SceneWorld.h>
#include <optional>
#include <cmath>

namespace Editor
{
    struct IkHandleTransform
    {
        /// <summary>IKハンドルのローカル座標を世界座標へ変換する行列を取得します。</summary>
        static bool Frame(const SceneRuntime::SceneWorld& world, const std::string& id, DirectX::XMFLOAT4X4& matrix)
        {
            return world.SkeletonWorldMatrix(id, matrix);
        }
        /// <summary>座標をワールド空間へ変換します。</summary>
        static std::optional<std::array<float, 3>> ToWorld(const SceneRuntime::AnimatorIkConstraint& constraint,
                                                           const DirectX::XMFLOAT4X4& matrix, bool hint);
        /// <summary>変更内容を反映します。</summary>
        static bool Apply(SceneRuntime::AnimatorIkConstraint& constraint, const DirectX::XMFLOAT4X4& matrix, bool hint,
                          std::array<float, 3> point);
    };
}
