#include "BlendTreeValidation.h"

namespace SceneRuntime::Detail
{
    double ClipDuration(const Engine::SkeletonData& rig, const std::string& name)
    {
        if (name.empty())
            return 1;
        const auto found = std::ranges::find(rig.clips, name, &Engine::SkeletalClip::name);
        if (found == rig.clips.end() || !std::isfinite(found->duration) || found->duration <= 0)
            throw std::runtime_error("Blend Tree clip missing or invalid: " + name);
        return found->duration;
    }
}
