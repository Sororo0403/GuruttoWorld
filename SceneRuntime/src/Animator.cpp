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
        if (!std::isfinite(value)) throw std::runtime_error("Nonfinite Animator parameter");
        if (transition.comparison==">") return value>transition.value;
        if (transition.comparison==">=") return value>=transition.value;
        if (transition.comparison=="<") return value<transition.value;
        if (transition.comparison=="<=") return value<=transition.value;
        if (transition.comparison=="==") return value==transition.value;
        return value!=transition.value;
    }
    std::vector<SceneRuntime::AnimatorMotionSample> Motions(const SceneRuntime::AnimatorComponent& component,
        const SceneRuntime::AnimatorStateDefinition& definition,const std::map<std::string,float>& parameters)
    {
        return definition.blendTree.empty() ? std::vector<SceneRuntime::AnimatorMotionSample>{{definition.clip,1,1}} :
            SceneRuntime::BlendTree::Samples(component.blendTrees,definition.blendTree,parameters);
    }
    std::vector<Engine::BonePose> Sample(const SceneRuntime::AnimatorComponent& component,
        const SceneRuntime::AnimatorStateDefinition& definition,const SceneRuntime::AnimatorState& state,
        const Engine::SkeletonData& rig,const std::map<std::string,float>& parameters)
    {
        return definition.blendTree.empty() ? Engine::Skeleton::Sample(rig,definition.clip,state.time,definition.loop) :
            SceneRuntime::BlendTree::SamplePose(Motions(component,definition,parameters),rig,state.normalizedTime,definition.loop);
    }
}
namespace SceneRuntime
{
    void Animator::ValidateParameters(const std::map<std::string,float>& parameters)
    {
        if (parameters.size()>64) throw std::runtime_error("Too many Animator parameters");
        for (const auto& [name,value] : parameters)
            if (name.empty() || name.size()>128 || name.find('\0')!=std::string::npos || !std::isfinite(value) || std::abs(value)>1000000)
                throw std::runtime_error("Invalid Animator parameter");
    }
    void Animator::Validate(const AnimatorComponent& component,const Engine::SkeletonData* rig)
    {
        if (component.states.empty() || component.states.size()>32 || component.transitions.size()>128) throw std::runtime_error("Invalid Animator state count");
        BlendTree::Validate(component.blendTrees,rig);
        ValidateParameters(component.parameters);
        AnimationEvents::Validate(component.events,rig);
        std::set<std::string> names;
        for (const auto& state : component.states)
        {
            if (state.name.empty() || state.name.size()>128 || state.name=="*" || state.name.find('\0')!=std::string::npos || !names.insert(state.name).second ||
                state.clip.size()>256 || state.clip.find('\0')!=std::string::npos || state.blendTree.size()>128 || state.blendTree.find('\0')!=std::string::npos || !std::isfinite(state.speed) || state.speed<0 || state.speed>1000) throw std::runtime_error("Invalid Animator state");
            if (rig && !state.clip.empty() && std::none_of(rig->clips.begin(),rig->clips.end(),[&](const auto& clip) { return clip.name==state.clip; })) throw std::runtime_error("Animator clip missing: "+state.clip);
            if (!state.blendTree.empty() && std::none_of(component.blendTrees.begin(),component.blendTrees.end(),[&](const auto& tree) { return tree.name==state.blendTree; }))
                throw std::runtime_error("Animator Blend Tree missing: "+state.blendTree);
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
        auto next=state;
        next.events.clear();
        ValidateParameters(next.parameterOverrides);
        auto values=component.parameters;
        for (const auto& [name,value] : parameters) values[name]=value;
        for (const auto& [name,value] : next.parameterOverrides) values[name]=value;
        const bool initializing=next.current.empty();
        if (initializing) next.current=component.initialState;
        if (!std::isfinite(next.time) || next.time<0 || !std::isfinite(next.normalizedTime) || next.normalizedTime<0) throw std::runtime_error("Invalid Animator clock");
        const auto& before=State(component,next.current);
        if (!component.enabled)
        {
            if (next.pose.empty()) { next.pose=Sample(component,before,next,rig,values); next.motions=Motions(component,before,values); }
            auto pose=next.pose; state=std::move(next); return pose;
        }
        const double duration=BlendTree::Duration(Motions(component,before,values),rig);
        for (const auto& transition : component.transitions)
        {
            const double phase=before.blendTree.empty() ? next.time/duration : next.normalizedTime;
            if (initializing || (transition.from!="*" && transition.from!=next.current) || transition.to==next.current ||
                (transition.exitTime>=0 && phase<transition.exitTime) || !Condition(transition,values)) continue;
            next.previousPose=next.pose.empty() ? Sample(component,before,next,rig,values) : next.pose;
            next.current=transition.to; next.time=next.normalizedTime=0; next.blendElapsed=0; next.blendDuration=transition.blendSeconds; next.eventsAtStart=true;
            break;
        }
        const auto& definition=State(component,next.current);
        next.motions=Motions(component,definition,values);
        const double cycleDuration=BlendTree::Duration(next.motions,rig);
        const double from=definition.blendTree.empty() ? next.time/cycleDuration : next.normalizedTime;
        const float fromWeight=next.previousPose.empty() || next.blendDuration<=0 ? 1 : std::clamp(next.blendElapsed/next.blendDuration,0.0f,1.0f);
        const float tick=static_cast<float>(std::min(seconds,0.1)); next.time+=tick*definition.speed;
        next.normalizedTime=definition.blendTree.empty() ? next.time/cycleDuration : next.normalizedTime+tick*definition.speed/cycleDuration;
        auto pose=definition.blendTree.empty() ? Engine::Skeleton::Sample(rig,definition.clip,next.time,definition.loop) :
            BlendTree::SamplePose(next.motions,rig,next.normalizedTime,definition.loop);
        if (!next.previousPose.empty() && next.blendDuration>0)
        {
            next.blendElapsed=std::min(next.blendElapsed+tick,next.blendDuration);
            pose=Engine::Skeleton::Blend(next.previousPose,pose,next.blendElapsed/next.blendDuration);
            if (next.blendElapsed>=next.blendDuration) next.previousPose.clear();
        }
        else next.previousPose.clear();
        const float toWeight=next.previousPose.empty() || next.blendDuration<=0 ? 1 : std::clamp(next.blendElapsed/next.blendDuration,0.0f,1.0f);
        next.events=AnimationEvents::Collect(component.events,rig,next.current,next.motions,from,next.normalizedTime,definition.loop,next.eventsAtStart,fromWeight,toWeight);
        if (next.normalizedTime>from) next.eventsAtStart=false;
        next.pose=pose; state=std::move(next); return pose;
    }
}
