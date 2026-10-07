#pragma once
#include <Engine/Animation/Skeleton.h>

namespace SceneRuntime
{
    struct AnimatorStateDefinition
    {
        std::string name="Idle",clip;
        float speed=1;
        bool loop=true;
        bool operator==(const AnimatorStateDefinition&) const = default;
    };
    struct AnimatorTransition
    {
        std::string from="Idle",to="Walk",parameter="speed",comparison=">";
        float value=0.05f,blendSeconds=0.2f,exitTime=-1;
        bool operator==(const AnimatorTransition&) const = default;
    };
    struct AnimatorComponent
    {
        std::string id="animator",initialState="Idle";
        bool enabled=true;
        std::vector<AnimatorStateDefinition> states{{}};
        std::vector<AnimatorTransition> transitions;
        bool operator==(const AnimatorComponent&) const = default;
    };
    struct AnimatorState
    {
        std::string current;
        double time=0;
        float blendElapsed=0,blendDuration=0;
        std::vector<Engine::BonePose> previousPose,pose;
    };
    class Animator final
    {
    public:
        static void Validate(const AnimatorComponent& component,const Engine::SkeletonData* rig=nullptr);
        static std::vector<Engine::BonePose> Advance(const AnimatorComponent& component,AnimatorState& state,
            const Engine::SkeletonData& rig,double seconds,const std::map<std::string,float>& parameters);
    };
}
