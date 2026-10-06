#pragma once
#include "SceneViewport.h"
#include <algorithm>
#include <Engine/Graphics/Camera.h>

namespace Editor
{
    inline constexpr const char* ModelPayload = "WP1_PROJECT_MODEL";

    // Place on the editor ground plane; horizon/sky drops use a point eight units along the ray.
    inline std::optional<std::array<float,3>> ModelDropPosition(const Engine::Camera& camera,
        const SceneViewport& viewport, float x, float y)
    {
        const auto ndc=viewport.ToNdc(x,y);
        if (!ndc) return std::nullopt;
        using namespace DirectX;
        const auto inverse=XMMatrixInverse(nullptr,camera.GetViewMatrix()*camera.GetProjectionMatrix());
        const auto nearPoint=XMVector3TransformCoord(XMVectorSet((*ndc)[0],(*ndc)[1],0,1),inverse);
        const auto farPoint=XMVector3TransformCoord(XMVectorSet((*ndc)[0],(*ndc)[1],1,1),inverse);
        XMFLOAT3 origin, direction;
        XMStoreFloat3(&origin,nearPoint);
        XMStoreFloat3(&direction,XMVector3Normalize(farPoint-nearPoint));
        float distance=8;
        if (std::abs(direction.y)>0.0001f)
        {
            const float ground=(0.08f-origin.y)/direction.y;
            if (ground>=0 && ground<=1000) distance=ground;
        }
        const std::array<float,3> position{origin.x+direction.x*distance,
            origin.y+direction.y*distance,origin.z+direction.z*distance};
        if (std::any_of(position.begin(),position.end(),[](float value) { return !std::isfinite(value); }))
            return std::nullopt;
        return position;
    }
}
