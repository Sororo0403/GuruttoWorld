#pragma once
#include <SceneRuntime/Scene/SceneWorld.h>
#include <SceneRuntime/Scene/SceneTransforms.h>
#include <algorithm>
#include <cmath>
#include <utility>

namespace Editor::GizmoTransform
{
    /// <summary>
    /// ハンドルの座標系は、親から継承したせん断や反転スケールに依存しません。
    /// </summary>
    bool Build(const SceneRuntime::SceneWorld& world, const SceneRuntime::ScenePlacement& current, bool scale,
               DirectX::XMFLOAT4X4& output);

    /// <summary>操作行列から回転を読み取ります。</summary>
    bool ReadRotation(const SceneRuntime::SceneWorld& world, const SceneRuntime::ScenePlacement& current,
                      const DirectX::XMFLOAT4X4& handle, SceneRuntime::ScenePlacement& output);

    /// <summary>操作前後の回転差分を求めます。</summary>
    bool RotationDelta(const SceneRuntime::SceneWorld& world, const SceneRuntime::ScenePlacement& current,
                       const DirectX::XMFLOAT4X4& handle, DirectX::XMFLOAT4X4& output);

    /// <summary>操作行列から拡縮を読み取ります。</summary>
    bool ReadScale(const SceneRuntime::ScenePlacement& current, const DirectX::XMFLOAT4X4& handle,
                   SceneRuntime::ScenePlacement& output);

    /// <summary>操作前後の拡縮差分を求めます。</summary>
    bool ScaleDelta(const SceneRuntime::SceneWorld& world, const SceneRuntime::ScenePlacement& current,
                    const DirectX::XMFLOAT4X4& handle, std::array<float, 3>& output);
}
