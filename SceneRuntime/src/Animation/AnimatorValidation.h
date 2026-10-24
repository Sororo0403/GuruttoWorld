#pragma once
#include <SceneRuntime/Animation/Animator.h>

namespace SceneRuntime
{
    /// <summary>重複を拒否してAnimatorが使用するボーン番号を取得します。</summary>
    size_t FindAnimatorBone(const Engine::SkeletonData& rig, const std::string& name);
}
