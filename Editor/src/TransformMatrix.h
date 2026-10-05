#pragma once
#include <SceneRuntime/SceneLayout.h>
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>

namespace Editor::TransformMatrix
{
    inline DirectX::XMFLOAT4X4 Compose(const SceneRuntime::ScenePlacement& value)
    {
        using namespace DirectX;
        XMFLOAT4X4 result;
        XMStoreFloat4x4(&result, XMMatrixScaling(value.scale[0], value.scale[1], value.scale[2]) *
            XMMatrixRotationX(value.rotation[0]) * XMMatrixRotationY(value.rotation[1]) *
            XMMatrixRotationZ(value.rotation[2]) *
            XMMatrixTranslation(value.position[0], value.position[1], value.position[2]));
        return result;
    }

    // Keep the existing mirror signs and choose the equivalent Euler angles nearest the inspector values.
    inline bool Read(const DirectX::XMFLOAT4X4& matrix, const SceneRuntime::ScenePlacement& reference,
        SceneRuntime::ScenePlacement& output)
    {
        for (const auto& row : matrix.m) for (float element : row) if (!std::isfinite(element)) return false;
        auto result = reference;
        float r[3][3]{};
        for (int i=0; i<3; ++i)
        {
            const float length = std::sqrt(matrix.m[i][0]*matrix.m[i][0] +
                matrix.m[i][1]*matrix.m[i][1] + matrix.m[i][2]*matrix.m[i][2]);
            if (!std::isfinite(length) || length < 1e-6f) return false;
            result.scale[i] = std::copysign(length, reference.scale[i]);
            for (int j=0; j<3; ++j) r[i][j] = matrix.m[i][j] / result.scale[i];
        }
        const float y = std::asin(std::clamp(-r[0][2], -1.0f, 1.0f));
        float x, z;
        if (std::abs(std::cos(y)) > 1e-4f)
        {
            x=std::atan2(r[1][2],r[2][2]);
            z=std::atan2(r[0][1],r[0][0]);
        }
        else
        {
            z=reference.rotation[2];
            x=y>0 ? std::atan2(r[1][0],r[1][1])+z : std::atan2(-r[1][0],r[1][1])-z;
        }
        const auto nearest = [&](std::array<float,3> angles)
        {
            for (int i=0;i<3;++i) angles[i]=reference.rotation[i]+
                std::remainder(angles[i]-reference.rotation[i],DirectX::XM_2PI);
            return angles;
        };
        const auto primary=nearest({x,y,z});
        const auto alternate=nearest({x+DirectX::XM_PI,DirectX::XM_PI-y,z+DirectX::XM_PI});
        const auto distance = [&](const auto& angles)
        {
            float sum=0;
            for (int i=0;i<3;++i) { const float d=angles[i]-reference.rotation[i]; sum+=d*d; }
            return sum;
        };
        result.rotation=distance(primary)<=distance(alternate) ? primary : alternate;
        result.position={matrix._41,matrix._42,matrix._43};
        const auto rebuilt=Compose(result);
        for (int i=0;i<4;++i) for (int j=0;j<4;++j)
            if (std::abs(rebuilt.m[i][j]-matrix.m[i][j]) > 0.001f*std::max(1.0f,std::abs(matrix.m[i][j]))) return false;
        output=result;
        return true;
    }
}
