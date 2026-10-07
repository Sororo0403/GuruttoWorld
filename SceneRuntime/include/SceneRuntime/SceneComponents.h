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
        std::string visibleWhen;
        bool operator==(const MeshRendererComponent&) const = default;
    };
    struct PlayerControllerComponent
    {
        std::string id="player";
        bool enabled=true;
        float moveSpeed=5;
        bool useGravity=false;
        float gravity=20;
        float jumpSpeed=7;
        bool operator==(const PlayerControllerComponent&) const = default;
    };
    struct BoxColliderComponent
    {
        std::string id="collider";
        bool enabled=true;
        std::array<float,3> center{};
        std::array<float,3> size{1,1,1};
        bool operator==(const BoxColliderComponent&) const = default;
    };
    struct RotatorComponent
    {
        std::string id="rotator";
        bool enabled=true;
        std::array<float,3> angularVelocity{0,90,0}; // Local Euler degrees per second.
        bool operator==(const RotatorComponent&) const = default;
    };
}
