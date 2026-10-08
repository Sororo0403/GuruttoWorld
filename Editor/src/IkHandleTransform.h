#pragma once
#include <SceneRuntime/Animator.h>
#include <SceneRuntime/SceneTransforms.h>
#include <optional>
#include <cmath>

namespace Editor {
struct IkHandleTransform {
    static std::optional<std::array<float,3>> ToWorld(const SceneRuntime::AnimatorIkConstraint& constraint,
        const DirectX::XMFLOAT4X4& matrix,bool hint) {
        const auto point=hint?constraint.hint:constraint.target;
        if(constraint.worldSpace) return point;
        if(!SceneRuntime::SceneTransforms::IsUsable(matrix)) return {};
        DirectX::XMFLOAT3 value;
        DirectX::XMStoreFloat3(&value,DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(point[0],point[1],point[2],1),DirectX::XMLoadFloat4x4(&matrix)));
        return std::array<float,3>{value.x,value.y,value.z};
    }
    static bool Apply(SceneRuntime::AnimatorIkConstraint& constraint,const DirectX::XMFLOAT4X4& matrix,bool hint,std::array<float,3> point) {
        if(!std::all_of(point.begin(),point.end(),[](float value){return std::isfinite(value);})) return false;
        if(!constraint.worldSpace) {
            if(!SceneRuntime::SceneTransforms::IsUsable(matrix)) return false;
            DirectX::XMFLOAT3 value;
            DirectX::XMStoreFloat3(&value,DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(point[0],point[1],point[2],1),DirectX::XMMatrixInverse(nullptr,DirectX::XMLoadFloat4x4(&matrix))));
            point={value.x,value.y,value.z};
        }
        if(!std::all_of(point.begin(),point.end(),[](float value){return std::isfinite(value) && std::abs(value)<=1000000;})) return false;
        (hint?constraint.hint:constraint.target)=point;return true;
    }
};
}
