#pragma once
#include <SceneRuntime/Animation/BlendTree.h>
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace SceneRuntime::Detail
{
    /// <summary>存在する有効なクリップの長さを取得します。</summary>
    inline double ClipDuration(const Engine::SkeletonData& rig, const std::string& name)
    {
        if (name.empty())
            return 1;
        const auto found = std::ranges::find(rig.clips, name, &Engine::SkeletalClip::name);
        if (found == rig.clips.end() || !std::isfinite(found->duration) || found->duration <= 0)
            throw std::runtime_error("Blend Tree clip missing or invalid: " + name);
        return found->duration;
    }
    /// <summary>ブレンド対象の重みと再生速度を検証します。</summary>
    inline void ValidateSample(const SceneRuntime::AnimatorMotionSample& sample)
    {
        if (!std::isfinite(sample.weight) || sample.weight < 0 || !std::isfinite(sample.speed) || sample.speed <= 0)
            throw std::runtime_error("Invalid Blend Tree sample");
    }
}
