#pragma once
#include <Engine/Graphics/Camera.h>
#include <optional>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace Editor
{
    // Fit a sphere around the transformed bounds while preserving the current viewing direction.
    inline std::optional<std::array<float,3>> FocusPosition(
        const std::array<std::array<float,3>,8>& corners, const Engine::Camera& camera)
    {
        using namespace DirectX;
        std::array<float,3> low=corners[0], high=corners[0], center{};
        for (const auto& corner : corners) for (int i=0;i<3;++i)
        {
            if (!std::isfinite(corner[i])) return std::nullopt;
            low[i]=std::min(low[i],corner[i]);
            high[i]=std::max(high[i],corner[i]);
        }
        for (int i=0;i<3;++i) center[i]=std::midpoint(low[i],high[i]);
        float radius=0.25f;
        for (const auto& corner : corners)
        {
            float squared=0;
            for (int i=0;i<3;++i) squared+=(corner[i]-center[i])*(corner[i]-center[i]);
            radius=std::max(radius,std::sqrt(squared));
        }
        XMFLOAT4X4 projection, inverse;
        XMStoreFloat4x4(&projection,camera.GetProjectionMatrix());
        XMStoreFloat4x4(&inverse,XMMatrixInverse(nullptr,camera.GetViewMatrix()));
        const float halfAngle=std::atan(1.0f/std::max(projection._11,projection._22));
        const float distance=radius*1.15f/std::sin(halfAngle)+0.1f;
        const float farClip=-projection._43/(projection._33-1.0f);
        if (!std::isfinite(distance) || distance+radius>=farClip) return std::nullopt;
        return std::array<float,3>{center[0]-inverse._31*distance,
            center[1]-inverse._32*distance,center[2]-inverse._33*distance};
    }
}
