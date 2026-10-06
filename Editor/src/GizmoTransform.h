#pragma once
#include <SceneRuntime/SceneWorld.h>
#include <SceneRuntime/SceneTransforms.h>
#include <algorithm>
#include <cmath>
#include <utility>

namespace Editor::GizmoTransform
{
    // The handle frame is independent of inherited shear and mirrored scales.
    inline bool Build(const SceneRuntime::SceneWorld& world, const SceneRuntime::ScenePlacement& current,
        bool scale, DirectX::XMFLOAT4X4& output)
    {
        DirectX::XMFLOAT4X4 rotation, position;
        if (!world.WorldRotation(current.id,rotation) || !world.WorldMatrix(current.id,position)) return false;
        auto matrix=DirectX::XMLoadFloat4x4(&rotation);
        if (scale) matrix=DirectX::XMMatrixScaling(current.scale[0],current.scale[1],current.scale[2])*matrix;
        DirectX::XMFLOAT4X4 candidate;
        DirectX::XMStoreFloat4x4(&candidate,matrix);
        candidate._41=position._41; candidate._42=position._42; candidate._43=position._43;
        if (!SceneRuntime::SceneTransforms::IsUsable(candidate)) return false;
        output=candidate;
        return true;
    }

    inline bool ReadRotation(const SceneRuntime::SceneWorld& world, const SceneRuntime::ScenePlacement& current,
        const DirectX::XMFLOAT4X4& handle, SceneRuntime::ScenePlacement& output)
    {
        auto local=handle;
        local._41=0; local._42=0; local._43=0;
        if (!current.parentId.empty())
        {
            DirectX::XMFLOAT4X4 parent;
            if (!world.WorldRotation(current.parentId,parent) ||
                !SceneRuntime::SceneTransforms::WorldToLocal(local,parent,local)) return false;
        }
        auto reference=current;
        reference.position={0,0,0}; reference.scale={1,1,1};
        auto result=reference;
        if (!SceneRuntime::SceneTransforms::ReadTransform(local,reference,result)) return false;
        // A rotation handle must remain orthonormal; do not silently discard scale changes.
        if (!std::all_of(result.scale.begin(),result.scale.end(),
            [](float value) { return std::abs(value-1)<=0.001f; })) return false;
        auto candidate=current;
        candidate.rotation=result.rotation;
        output=std::move(candidate);
        return true;
    }

    inline bool RotationDelta(const SceneRuntime::SceneWorld& world, const SceneRuntime::ScenePlacement& current,
        const DirectX::XMFLOAT4X4& handle, DirectX::XMFLOAT4X4& output)
    {
        SceneRuntime::ScenePlacement validated;
        DirectX::XMFLOAT4X4 previous;
        if (!ReadRotation(world,current,handle,validated) || !world.WorldRotation(current.id,previous)) return false;
        auto next=handle;
        next._41=0; next._42=0; next._43=0;
        DirectX::XMFLOAT4X4 delta;
        DirectX::XMStoreFloat4x4(&delta,DirectX::XMMatrixTranspose(DirectX::XMLoadFloat4x4(&previous))*
            DirectX::XMLoadFloat4x4(&next));
        if (!SceneRuntime::SceneTransforms::IsUsable(delta)) return false;
        output=delta;
        return true;
    }

    inline bool ReadScale(const SceneRuntime::ScenePlacement& current,
        const DirectX::XMFLOAT4X4& handle, SceneRuntime::ScenePlacement& output)
    {
        auto result=current;
        if (!SceneRuntime::SceneTransforms::ReadTransform(handle,current,result)) return false;
        auto candidate=current;
        candidate.scale=result.scale;
        output=std::move(candidate);
        return true;
    }
}
