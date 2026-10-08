#include <SceneRuntime/Animator.h>
#include <Engine/Animation/TwoBoneIk.h>
#include <Engine/Animation/RootMotion.h>
#include <SceneRuntime/SceneTransforms.h>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace
{
    Engine::BonePose RootDelta(const Engine::SkeletonData& rig,size_t bone,const std::vector<SceneRuntime::AnimatorMotionSample>& motions,double from,double to,bool loop)
    {
        std::vector<Engine::BonePose> blended; double total=0;
        for (const auto& motion : motions) if (motion.weight>0)
        {
            const auto matrix=Engine::RootMotion::Delta(rig,bone,motion.clip,from,to,loop);
            DirectX::XMVECTOR scale,rotation,position;
            if (!DirectX::XMMatrixDecompose(&scale,&rotation,&position,DirectX::XMLoadFloat4x4(&matrix))) throw std::runtime_error("Invalid root motion delta");
            DirectX::XMFLOAT3 translation; DirectX::XMFLOAT4 quaternion;
            DirectX::XMStoreFloat3(&translation,position); DirectX::XMStoreFloat4(&quaternion,DirectX::XMQuaternionNormalize(rotation));
            Engine::BonePose pose; pose.position={translation.x,translation.y,translation.z}; pose.rotation={quaternion.x,quaternion.y,quaternion.z,quaternion.w};
            if (blended.empty()) blended={pose};
            else blended=Engine::Skeleton::Blend(blended,{pose},static_cast<float>(motion.weight/(total+motion.weight)));
            total+=motion.weight;
        }
        if (blended.empty()) throw std::runtime_error("Root motion weights empty");
        return blended.front();
    }
    size_t Bone(const Engine::SkeletonData& rig,const std::string& name)
    {
        size_t result=rig.nodes.size();
        for (size_t index=0;index<rig.nodes.size();++index) if (rig.nodes[index].name==name)
        { if (result!=rig.nodes.size()) throw std::runtime_error("Ambiguous IK bone: "+name); result=index; }
        if (result==rig.nodes.size()) throw std::runtime_error("Missing IK bone: "+name);
        return result;
    }
    std::vector<Engine::BonePose> SolveIk(const SceneRuntime::AnimatorComponent& component,const Engine::SkeletonData& rig,std::vector<Engine::BonePose> pose,const DirectX::XMFLOAT4X4* modelWorld)
    {
        for (const auto& item : component.ik) if (item.enabled && item.weight>0)
        {
            Engine::TwoBoneIkConstraint constraint;
            constraint.root=Bone(rig,item.root); constraint.middle=Bone(rig,item.middle); constraint.tip=Bone(rig,item.tip);
            constraint.target=item.target; constraint.hint=item.hint; constraint.weight=item.weight;
            if (item.worldSpace)
            {
                using namespace DirectX;
                if (!modelWorld || !std::isfinite(rig.importScale) || rig.importScale<=0) throw std::runtime_error("World-space IK requires model transform");
                const auto skeletonWorld=XMLoadFloat4x4(&rig.inverseRoot)*XMMatrixScaling(rig.importScale,rig.importScale,rig.importScale)*XMLoadFloat4x4(modelWorld);
                XMFLOAT4X4 transform; XMStoreFloat4x4(&transform,skeletonWorld);
                if (!SceneRuntime::SceneTransforms::IsUsable(transform)) throw std::runtime_error("Invalid world-space IK transform");
                const auto inverse=XMMatrixInverse(nullptr,skeletonWorld);
                for (auto* point : {&constraint.target,&constraint.hint})
                {
                    XMFLOAT3 converted;
                    XMStoreFloat3(&converted,XMVector3TransformCoord(XMVectorSet((*point)[0],(*point)[1],(*point)[2],1),inverse));
                    *point={converted.x,converted.y,converted.z};
                }
            }
            pose=Engine::TwoBoneIk::Solve(rig,pose,constraint);
        }
        return pose;
    }
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
    void Animator::ValidateIkTarget(const AnimatorIkTarget& target)
    {
        if (!std::isfinite(target.weight) || target.weight<0 || target.weight>1) throw std::runtime_error("Invalid IK target weight");
        for (const auto& point : {target.target,target.hint})
            if (std::ranges::any_of(point,[](float value) { return !std::isfinite(value) || std::abs(value)>1000000; })) throw std::runtime_error("Invalid IK target point");
    }
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
        if (component.rootBone.size()>256 || component.rootBone.find('\0')!=std::string::npos || (component.rootMotion && component.rootBone.empty())) throw std::runtime_error("Invalid root motion bone name");
        if (rig && component.rootMotion) static_cast<void>(Bone(*rig,component.rootBone));
        if (component.ik.size()>16) throw std::runtime_error("Too many IK constraints");
        std::set<std::string> ikNames;
        for (const auto& item : component.ik)
        {
            for (const auto& name : {item.name,item.root,item.middle,item.tip})
                if (name.empty() || name.size()>256 || name.find('\0')!=std::string::npos) throw std::runtime_error("Invalid IK name");
            if (!ikNames.insert(item.name).second || !std::isfinite(item.weight) || item.weight<0 || item.weight>1)
                throw std::runtime_error("Invalid IK weight or duplicate name");
            for (const auto& point : {item.target,item.hint})
                if (std::ranges::any_of(point,[](float value) { return !std::isfinite(value) || std::abs(value)>1000000; })) throw std::runtime_error("Invalid IK point");
            if (item.root==item.middle || item.root==item.tip || item.middle==item.tip) throw std::runtime_error("Invalid IK chain");
            if (rig)
            {
                const auto root=Bone(*rig,item.root),middle=Bone(*rig,item.middle),tip=Bone(*rig,item.tip);
                if (rig->nodes[middle].parent!=static_cast<int>(root) || rig->nodes[tip].parent!=static_cast<int>(middle)) throw std::runtime_error("IK bones must be directly connected");
            }
        }
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
        const Engine::SkeletonData& rig,double seconds,const std::map<std::string,float>& parameters,const DirectX::XMFLOAT4X4* modelWorld)
    {
        if (!std::isfinite(seconds) || seconds<0) throw std::runtime_error("Invalid Animator time");
        Validate(component,&rig);
        auto next=state;
        next.rootDelta={};
        auto effective=component;
        if (next.ikOverrides.size()>16) throw std::runtime_error("Too many IK overrides");
        for (const auto& [name,target] : next.ikOverrides)
        {
            ValidateIkTarget(target);
            const auto found=std::ranges::find_if(effective.ik,[&](const auto& item) { return item.name==name; });
            if (found==effective.ik.end()) throw std::runtime_error("IK override constraint missing: "+name);
            found->target=target.target; found->hint=target.hint; found->weight=target.weight; found->worldSpace=target.worldSpace;
        }
        next.events.clear();
        ValidateParameters(next.parameterOverrides);
        auto values=component.parameters;
        for (const auto& [name,value] : parameters) values[name]=value;
        for (const auto& [name,value] : next.parameterOverrides) values[name]=value;
        const bool initializing=next.current.empty();
        if (initializing) next.current=component.initialState;
        if (!std::isfinite(next.time) || next.time<0 || !std::isfinite(next.normalizedTime) || next.normalizedTime<0) throw std::runtime_error("Invalid Animator clock");
        const auto& before=State(component,next.current);
        const auto StripRoot=[&](std::vector<Engine::BonePose>& pose) {
            if (!component.rootMotion) return;
            const auto bone=Bone(rig,component.rootBone); pose.at(bone).position=rig.nodes[bone].rest.position; pose.at(bone).rotation=rig.nodes[bone].rest.rotation;
        };
        if (!component.enabled)
        {
            if (next.pose.empty()) { next.basePose=Sample(component,before,next,rig,values); StripRoot(next.basePose); next.pose=SolveIk(effective,rig,next.basePose,modelWorld); next.motions=Motions(component,before,values); }
            auto pose=next.pose; state=std::move(next); return pose;
        }
        const double duration=BlendTree::Duration(Motions(component,before,values),rig);
        for (const auto& transition : component.transitions)
        {
            const double phase=before.blendTree.empty() ? next.time/duration : next.normalizedTime;
            if (initializing || (transition.from!="*" && transition.from!=next.current) || transition.to==next.current ||
                (transition.exitTime>=0 && phase<transition.exitTime) || !Condition(transition,values)) continue;
            next.previousPose=next.basePose.empty() ? Sample(component,before,next,rig,values) : next.basePose;
            StripRoot(next.previousPose);
            if (component.rootMotion)
            {
                next.previousRootMotions=Motions(component,before,values); next.previousRootPhase=phase;
                next.previousRootSpeed=before.speed; next.previousRootLoop=before.loop;
            }
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
        if (component.rootMotion)
        {
            const auto bone=Bone(rig,component.rootBone);
            next.rootDelta=RootDelta(rig,bone,next.motions,from,next.normalizedTime,definition.loop);
            if (!next.previousRootMotions.empty() && next.blendDuration>0 && fromWeight<1)
            {
                const double previousTo=next.previousRootPhase+tick*next.previousRootSpeed/BlendTree::Duration(next.previousRootMotions,rig);
                const auto outgoing=RootDelta(rig,bone,next.previousRootMotions,next.previousRootPhase,previousTo,next.previousRootLoop);
                next.previousRootPhase=previousTo;
                next.rootDelta=Engine::Skeleton::Blend({outgoing},{next.rootDelta},(fromWeight+toWeight)*.5f).front();
            }
            StripRoot(pose);
        }
        if (!component.rootMotion || toWeight>=1) next.previousRootMotions.clear();
        next.basePose=pose; pose=SolveIk(effective,rig,std::move(pose),modelWorld);
        next.pose=pose; state=std::move(next); return pose;
    }
}
