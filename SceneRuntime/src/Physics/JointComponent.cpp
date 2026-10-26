#include <SceneRuntime/Physics/JointComponent.h>

namespace SceneRuntime
{
    auto JointComponent::Validate() const -> void
    {
        if (type != "fixed" && type != "hinge" && type != "distance" && type != "swingTwist")
            throw std::runtime_error("Unknown Joint type");
        for (const auto& vector : {anchor, connectedAnchor, axis})
            if (std::any_of(vector.begin(), vector.end(),
                            [](float v)
                            {
                                return !Engine::InRange(v, -100000, 100000);
                            }))
                throw std::runtime_error("Invalid Joint vector");
        const float length = axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2];
        if (length < 0.000001f || !std::isfinite(length))
            throw std::runtime_error("Joint axis cannot be zero");
        ValidateLimits();
    }

    auto JointComponent::ValidateLimits() const -> void
    {
        const auto range = [](float value, float low, float high)
        {
            return std::isfinite(value) && value >= low && value <= high;
        };
        if (!range(minimum, -180, 0) || !range(maximum, 0, 180) || !range(swing, 0, 180))
            throw std::runtime_error("Invalid Joint angle limits");
        if (!range(minDistance, 0, 100000) || !range(maxDistance, minDistance, 100000))
            throw std::runtime_error("Invalid Joint distance limits");
    }
}
