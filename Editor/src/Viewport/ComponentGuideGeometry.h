#pragma once
#include <SceneRuntime/Scene/SceneLayout.h>
#include <vector>
#include <cmath>
#include <numbers>
#include <algorithm>

namespace Editor
{
    struct ComponentGuideGeometry
    {
        using Point = std::array<float, 3>;
        using Segment = std::array<Point, 2>;
        using Lines = std::vector<Segment>;
        /// <summary>箱形状を描画します。</summary>
        static Lines Box(Point center, Point half);
        /// <summary>球形状を描画します。</summary>
        static Lines Sphere(float radius, Point center = {});
        /// <summary>カプセル形状を描画します。</summary>
        static Lines Capsule(const SceneRuntime::BoxColliderComponent& c, Point scale);
        /// <summary>コライダーの設定を反映します。</summary>
        static Lines Collider(const SceneRuntime::BoxColliderComponent& c, Point scale = {1, 1, 1});
        /// <summary>カメラの設定を反映します。</summary>
        static Lines Camera(const SceneRuntime::CameraComponent& c);
        /// <summary>スポット光の形状を描画します。</summary>
        static Lines Spot(const SceneRuntime::SpotLightComponent& c);
    };
}
