#include <SceneRuntime/Animator.h>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace
{
    const SceneRuntime::AnimatorStateDefinition& State(const SceneRuntime::AnimatorComponent& component,const std::string& name)
    {
        const auto found=std::find_if(component.states.begin(),component.states.end(),[&](const auto& state) { return state.name==name; });
        if (found==component.states.end()) throw std::runtime_error("Animator state missing: "+name);
        return *found;
    }
    bool Condition(const SceneRuntime::AnimatorTransition& transition,const std::map<std::string,float>& parameters)
    {
        if (transition.parameter.empty()) return true;
        const auto found=parameters.find(transition.parameter); const float value=found==parameters.end() ? 0 : found->second;
        if (transition.comparison==">") return value>transition.value;
        if (transition.comparison==">=") return value>=transition.value;
        if (transition.comparison=="<") return value<transition.value;
        if (transition.comparison=="<=") return value<=transition.value;
        if (transition.comparison=="==") return value==transition.value;
        return value!=transition.value;
    }
}
namespace SceneRuntime
{
    void Animator::Validate(const AnimatorComponent& component,const Engine::SkeletonData* rig)
    {
        if (component.states.empty() || component.states.size()>32 || component.transitions.size()>128) throw std::runtime_error("Invalid Animator state count");
        std::set<std::string> names;
        for (const auto& state : component.states)
        {
            if (state.name.empty() || state.name.size()>128 || state.name=="*" || state.name.find('\0')!=std::string::npos || !names.insert(state.name).second ||
                state.clip.size()>256 || state.clip.find('\0')!=std::string::npos || !std::isfinite(state.speed) || state.speed<0 || state.speed>1000) throw std::runtime_error("Invalid Animator state");
            if (rig && !state.clip.empty() && std::none_of(rig->clips.begin(),rig->clips.end(),[&](const auto& clip) { return clip.name==state.clip; })) throw std::runtime_error("Animator clip missing: "+state.clip);
        }
        if (!names.contains(component.initialState)) throw std::runtime_error("Animator initial state missing");
        for (const auto& transition : component.transitions)
        {
            const std::set<std::string> comparisons{">",">=","<","<=","==","!="};
            if ((transition.from!="*" && !names.contains(transition.from)) || !names.contains(transition.to) || !comparisons.contains(transition.comparison) ||
                transition.parameter.size()>128 || transition.parameter.find('\0')!=std::string::npos || !std::isfinite(transition.value) || std::abs(transition.value)>1000000 ||
                !std::isfinite(transition.blendSeconds) || transition.blendSeconds<0 || transition.blendSeconds>60 ||
                !std::isfinite(transition.exitTime) || transition.exitTime<-1 || transition.exitTime>100) throw std::runtime_error("Invalid Animator transition");
        }
    }
    std::vector<Engine::BonePose> Animator::Advance(const AnimatorComponent& component,AnimatorState& state,
        const Engine::SkeletonData& rig,double seconds,const std::map<std::string,float>& parameters)
    {
        if (!std::isfinite(seconds) || seconds<0) throw std::runtime_error("Invalid Animator time");
        Validate(component,&rig);
        const bool initializing=state.current.empty();
        if (initializing) state.current=component.initialState;
        if (!component.enabled) return state.pose.empty() ? Engine::Skeleton::Sample(rig,State(component,state.current).clip,state.time,State(component,state.current).loop) : state.pose;
        const auto& before=State(component,state.current);
        float duration=1;
        for (const auto& clip : rig.clips) if (clip.name==before.clip) duration=clip.duration;
        for (const auto& transition : component.transitions)
        {
            if (initializing || (transition.from!="*" && transition.from!=state.current) || transition.to==state.current ||
                (transition.exitTime>=0 && state.time<transition.exitTime*duration) || !Condition(transition,parameters)) continue;
            state.previousPose=state.pose.empty() ? Engine::Skeleton::Sample(rig,before.clip,state.time,before.loop) : state.pose;
            state.current=transition.to; state.time=0; state.blendElapsed=0; state.blendDuration=transition.blendSeconds;
            break;
        }
        const auto& definition=State(component,state.current);
        const float tick=static_cast<float>(std::min(seconds,0.1)); state.time+=tick*definition.speed;
        auto pose=Engine::Skeleton::Sample(rig,definition.clip,state.time,definition.loop);
        if (!state.previousPose.empty() && state.blendDuration>0)
        {
            state.blendElapsed=std::min(state.blendElapsed+tick,state.blendDuration);
            pose=Engine::Skeleton::Blend(state.previousPose,pose,state.blendElapsed/state.blendDuration);
            if (state.blendElapsed>=state.blendDuration) state.previousPose.clear();
        }
        else state.previousPose.clear();
        state.pose=pose; return pose;
    }
}
