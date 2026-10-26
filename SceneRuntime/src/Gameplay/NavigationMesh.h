#pragma once
#include <SceneRuntime/Scene/SceneLayout.h>

namespace SceneRuntime::NavigationMesh
{
    /// <summary>ワールド空間の地形と障害物を Recast へ渡し、保存用セルへ通行可否を戻します。</summary>
    void Bake(const SceneLayout& layout, const ScenePlacement& owner, const std::vector<DirectX::XMFLOAT4X4>& matrices,
              const DirectX::XMFLOAT4X4& transform, NavMeshComponent& mesh);

    /// <summary>保存済みセルを Recast のポリゴンへ変換し、Detour でローカル座標の経路を求めます。</summary>
    std::vector<std::array<float, 3>> Path(const NavMeshComponent& mesh, const std::array<float, 3>& start,
                                           const std::array<float, 3>& destination);
}
