#pragma once
#include <array>
#include <filesystem>
#include <string>

namespace SceneRuntime
{
    // IDs are stable within their owning object. Transform is mandatory and has reserved ID "transform".
    struct MeshRendererComponent
    {
        std::string id="mesh";
        bool enabled=true;
        std::filesystem::path model;
        bool operator==(const MeshRendererComponent&) const = default;
    };
    struct RotatorComponent
    {
        std::string id="rotator";
        bool enabled=true;
        std::array<float,3> angularVelocity{0,90,0}; // Local Euler degrees per second.
        bool operator==(const RotatorComponent&) const = default;
    };
}
