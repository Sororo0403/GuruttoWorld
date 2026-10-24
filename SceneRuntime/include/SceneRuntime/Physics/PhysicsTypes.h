#pragma once
#include <array>
#include <string>

namespace SceneRuntime
{
    struct PhysicsRayHit
    {
        std::string object;
        float distance = 0;
        std::array<float, 3> position{}, normal{};
        bool trigger = false;
    };
    struct PhysicsContactEvent
    {
        std::string first, second, phase;
        bool trigger = false;
    };
}
