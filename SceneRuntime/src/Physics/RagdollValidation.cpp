#include <SceneRuntime/Physics/Ragdoll.h>
#include <SceneRuntime/Scene/SceneTransforms.h>
#include <Engine/Core/Validation.h>
#include <algorithm>
#include <bit>
#include <stdexcept>

namespace SceneRuntime
{
    void RagdollComponent::Validate() const
    {
        if (!Engine::InRange(weight, 0, 1) || !Engine::PositiveRange(bodyMass, 100000) ||
            !Engine::PositiveRange(radius, 100) || !Engine::InRange(swing, 0, 180) || !Engine::InRange(twist, 0, 180) ||
            bones.size() > 256)
            throw std::runtime_error("Invalid Ragdoll settings");
        std::set<std::string> names, bodies;
        for (const auto& bone : bones)
        {
            if (!Engine::ValidString(bone.bone, 128, true) || !Engine::ValidString(bone.body, 256, true) ||
                !names.insert(bone.bone).second || !bodies.insert(bone.body).second)
                throw std::runtime_error("Invalid or duplicate Ragdoll binding");
            if (std::any_of(std::begin(bone.bindOffset), std::end(bone.bindOffset),
                            [](const auto value)
                            {
                                return !Engine::InRange(value, -100000, 100000);
                            }))
                throw std::runtime_error("Invalid Ragdoll bind offset");
            if (!SceneTransforms::IsUsable(std::bit_cast<DirectX::XMFLOAT4X4>(bone.bindOffset)))
                throw std::runtime_error("Singular Ragdoll bind offset");
        }
    }
}
