#pragma once

#include <array>

namespace Engine
{
    struct DirectionalLight
    {
        // ワールド空間で光が進む方向です。ゼロの場合、直接光は無効になります。
        std::array<float, 3> direction{ 0.4f, -0.7f, 1.0f };
        std::array<float, 3> color{ 1.0f, 1.0f, 1.0f };
        float intensity = 0.8f;
        float ambientIntensity = 0.2f;
        // Blinn-Phong 反射の強さと鋭さです。
        float specularStrength = 0.4f;
        float shininess = 32.0f;
        // false の場合は照明計算を行わず、テクスチャの色をそのまま使用します。
        bool enabled = true;
    };
}
