#pragma once
#include <Engine/Animation/Skeleton.h>
#include <SceneRuntime/BlendTree.h>
#include <SceneRuntime/AnimationEvents.h>

namespace SceneRuntime
{
    struct AnimatorIkConstraint
    {
        std::string name="IK",root,middle,tip;
        bool enabled=true;
        std::array<float,3> target{},hint{0,0,1};
        float weight=1;
        bool worldSpace=false;
        bool operator==(const AnimatorIkConstraint&) const = default;
    };
    struct AnimatorStateDefinition
    {
        std::string name="Idle",clip;
        float speed=1;
        bool loop=true;
        std::string blendTree;
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
        std::vector<AnimatorBlendTree> blendTrees;
        std::map<std::string,float> parameters;
        std::vector<AnimatorEventKey> events;
        std::vector<AnimatorIkConstraint> ik;
        bool operator==(const AnimatorComponent&) const = default;
    };
    struct AnimatorState
    {
        std::string current;
        double time=0;
        double normalizedTime=0;
        float blendElapsed=0,blendDuration=0;
        std::vector<Engine::BonePose> previousPose,pose;
        std::vector<Engine::BonePose> basePose;
        std::vector<AnimatorMotionSample> motions;
        std::map<std::string,float> parameterOverrides;
        bool eventsAtStart=true;
        std::vector<AnimatorEventOccurrence> events;
    };
    class Animator final
    {
    public:
        /// <summary>状態・遷移・Blend Treeと参照クリップを検証します。</summary>
        static void Validate(const AnimatorComponent& component,const Engine::SkeletonData* rig=nullptr);
        /// <summary>保存値・個体ごとの上書きパラメーターの名前・値・最大64件を検証します。</summary>
        static void ValidateParameters(const std::map<std::string,float>& parameters);
        /// <summary>状態と同期したBlend Treeを進めます。評価に失敗した場合は実行状態を保持します。</summary>
        static std::vector<Engine::BonePose> Advance(const AnimatorComponent& component,AnimatorState& state,
            const Engine::SkeletonData& rig,double seconds,const std::map<std::string,float>& parameters,
            const DirectX::XMFLOAT4X4* modelWorld=nullptr);
    };
}
