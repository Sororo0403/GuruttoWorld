#pragma once
#include <Engine/Animation/RootMotion.h>
#include <SceneRuntime/Animator.h>
#include "../SceneRuntime/src/AnimatorJson.h"
#include <cmath>
#include <limits>
#include <stdexcept>
namespace RootMotionValidation
{
    inline void Require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
    inline bool Near(const DirectX::XMFLOAT4X4& first,const DirectX::XMFLOAT4X4& second,float tolerance=.0001f)
    {
        for (size_t row=0;row<4;++row) for (size_t column=0;column<4;++column)
            if (std::abs(first.m[row][column]-second.m[row][column])>tolerance) return false;
        return true;
    }
    inline void Run()
    {
        using namespace Engine; using namespace DirectX;
        SkeletonData rig; rig.nodes={{"root",-1,{}}};
        SkeletalClip walk; walk.name="Walk"; walk.duration=2;
        walk.tracks[0].positions={{0,{3,0,0}},{2,{5,0,0}}}; rig.clips.push_back(walk);
        Require(std::abs(RootMotion::Sample(rig,0,"Walk",1.25,true)._41-5.5f)<.0001f,"root translation continues after loop");
        Require(std::abs(RootMotion::Delta(rig,0,"Walk",.9,1.1,true)._41-.4f)<.0001f,"root delta crosses boundary without reset");
        Require(std::abs(RootMotion::Delta(rig,0,"Walk",0,3.25,true)._41-6.5f)<.0001f,"root delta handles several cycles");
        Require(std::abs(RootMotion::Delta(rig,0,"Walk",1000000000000.0,1000000000000.25,true)._41-.5f)<.0001f,"root delta preserves small movement after long playback");
        Require(std::abs(RootMotion::Sample(rig,0,"Walk",20,false)._41-5)<.0001f,"nonlooping root clamps at endpoint");
        XMFLOAT4X4 identity; XMStoreFloat4x4(&identity,XMMatrixIdentity());
        Require(Near(RootMotion::Delta(rig,0,"Walk",1.1,5,false),identity),"completed nonloop root has zero delta");
        Require(Near(RootMotion::Delta(rig,0,"",0,100,true),identity),"rest motion has zero delta");
        rig.clips[0].tracks[0].positions={{0,{0,0,0}},{2,{1,0,0}}};
        rig.clips[0].tracks[0].rotations={{0,{0,0,0,1}},{2,{0,0,std::sin(XM_PIDIV4),std::cos(XM_PIDIV4)}}};
        const auto turn=RootMotion::Sample(rig,0,"Walk",2,true);
        Require(std::abs(turn._41-1)<.0001f && std::abs(turn._42-1)<.0001f,"turning cycle rotates subsequent displacement");
        const auto first=RootMotion::Sample(rig,0,"Walk",.3,true),last=RootMotion::Sample(rig,0,"Walk",2.7,true);
        const auto delta=RootMotion::Delta(rig,0,"Walk",.3,2.7,true); XMFLOAT4X4 composed;
        XMStoreFloat4x4(&composed,XMLoadFloat4x4(&delta)*XMLoadFloat4x4(&first));
        Require(Near(composed,last),"root delta reconstructs destination rigid pose");
        const auto before=RootMotion::Sample(rig,0,"Walk",1-1e-6,true),after=RootMotion::Sample(rig,0,"Walk",1+1e-6,true);
        Require(Near(before,after,.00002f),"turning root transform remains continuous at boundary");
        const auto repeated=RootMotion::Sample(rig,0,"Walk",1000000,true);
        Require(std::abs(XMVectorGetX(XMMatrixDeterminant(XMLoadFloat4x4(&repeated)))-1)<.0001f,"many root cycles retain rigid unit scale");
        rig.clips[0].tracks[0].positions={{0,{3,0,0}},{2,{4,0,0}}};
        rig.clips[0].tracks[0].rotations={{0,{0,0,std::sin(XM_PI/8),std::cos(XM_PI/8)}},{2,{0,0,std::sin(3*XM_PI/8),std::cos(3*XM_PI/8)}}};
        const auto offsetStart=RootMotion::Sample(rig,0,"Walk",2,true);
        Require(std::abs(offsetStart._41-4)<.0001f && std::abs(offsetStart._42-1)<.0001f,"root cycles support translated and rotated starting pose");
        auto Reject=[&](size_t bone,const std::string& clip,double from,double to) {
            bool rejected=false; try { RootMotion::Delta(rig,bone,clip,from,to,true); } catch (const std::exception&) { rejected=true; }
            Require(rejected,"invalid root motion input rejected");
        };
        Reject(1,"Walk",0,1); Reject(0,"missing",0,1); Reject(0,"Walk",1,0);
        Reject(0,"Walk",0,std::numeric_limits<double>::quiet_NaN()); Reject(0,"Walk",-1,0);
        Reject(0,"Walk",0,9007199254740992.0);
        using namespace SceneRuntime;
        rig.clips[0].tracks[0].positions={{0,{0,0,0}},{2,{2,0,0}}}; rig.clips[0].tracks[0].rotations.clear();
        auto run=rig.clips[0]; run.name="Run"; run.tracks[0].positions.back().value={4,0,0}; rig.clips.push_back(run);
        AnimatorComponent animator; animator.states[0].clip="Walk"; animator.rootMotion=true; animator.rootBone="root";
        AnimatorState state; Animator::Advance(animator,state,rig,.1,{});
        Require(std::abs(state.rootDelta.position[0]-.1f)<.0001f && state.pose[0].position==rig.nodes[0].rest.position,"Animator extracts root delta and leaves in-place bone pose");
        Require(ReadAnimator(WriteAnimator(animator),animator.id,true)==animator,"root motion Animator schema roundtrip");
        animator.states.push_back({"Run","Run",1,true}); animator.transitions.push_back({"Idle","Run","go",">",0,.2f,-1});
        Animator::Advance(animator,state,rig,.1,{{"go",1.0f}});
        Require(std::abs(state.rootDelta.position[0]-.125f)<.0001f,"root motion blends advancing outgoing and incoming velocities");
        Animator::Advance(animator,state,rig,.1,{});
        Require(std::abs(state.rootDelta.position[0]-.175f)<.0001f && state.previousRootMotions.empty(),"root motion fade completes without velocity discontinuity");
        animator.enabled=false; const auto frozen=state.time; Animator::Advance(animator,state,rig,.1,{});
        Require(state.time==frozen && state.rootDelta.position==std::array<float,3>{},"disabled Animator clears delta and freezes clock");
        animator.enabled=true; animator.transitions.clear(); animator.states.resize(1); animator.states[0].blendTree="Motion";
        AnimatorBlendTree tree; tree.name="Motion"; tree.type=AnimatorBlendType::Direct;
        AnimatorBlendMotion firstMotion; firstMotion.clip="Walk"; firstMotion.parameter="walk";
        auto secondMotion=firstMotion; secondMotion.clip="Run"; secondMotion.parameter="run"; tree.children={firstMotion,secondMotion}; animator.blendTrees={tree};
        AnimatorState blended; Animator::Advance(animator,blended,rig,.1,{{"walk",1.0f},{"run",1.0f}});
        Require(std::abs(blended.rootDelta.position[0]-.15f)<.0001f,"Blend Tree root delta mixes synchronized clip motions");
        animator.states[0].blendTree.clear(); animator.blendTrees.clear();
        rig.clips[0].tracks[0].rotations={{0,{0,0,0,1}},{2,{0,0,std::sin(XM_PIDIV4),std::cos(XM_PIDIV4)}}};
        rig.clips[0].tracks[0].scales={{0,{1,1,1}},{2,{2,2,2}}};
        AnimatorState rotating; Animator::Advance(animator,rotating,rig,.1,{});
        Require(rotating.rootDelta.rotation[2]>.03f && rotating.pose[0].rotation==rig.nodes[0].rest.rotation,"Animator extracts rotation and restores root rest orientation");
        Require(std::abs(rotating.pose[0].scale[0]-1.05f)<.0001f && rotating.rootDelta.scale==std::array<float,3>{1,1,1},"root motion retains skeletal scale without extracting it");
        const auto validTime=blended.time; const auto validDelta=blended.rootDelta.position; animator.rootBone="missing"; bool invalidBone=false;
        try { Animator::Advance(animator,blended,rig,.1,{}); } catch (const std::exception&) { invalidBone=true; }
        Require(invalidBone && blended.time==validTime && blended.rootDelta.position==validDelta,"failed root extraction preserves Animator state");
        animator.rootBone="root"; rig.nodes[0].rest.scale={7,7,7}; rig.importScale=3;
        XMStoreFloat4x4(&rig.inverseRoot,XMMatrixIdentity()); AnimatorState displacement; displacement.rootDelta.position={.1f,0,0};
        const auto modelDelta=Animator::RootDeltaMatrix(animator,displacement,rig);
        Require(std::abs(modelDelta._41-.3f)<.0001f,"root model delta applies import scale without applying root bone scale twice");
        SkeletonData hierarchy; hierarchy.nodes={{"parent",-1,{}},{"root",0,{}}};
        hierarchy.nodes[1].rest.position={0,1,0};
        XMStoreFloat4x4(&hierarchy.inverseRoot,XMMatrixIdentity());
        SkeletalClip parentMotion; parentMotion.name="Parent"; parentMotion.duration=1;
        parentMotion.tracks[0].positions={{0,{0,0,0}},{1,{1,0,0}}};
        parentMotion.tracks[0].rotations={{0,{0,0,0,1}},{1,{0,0,std::sin(XM_PIDIV4),std::cos(XM_PIDIV4)}}};
        parentMotion.tracks[0].scales={{0,{1,1,1}},{1,{2,2,2}}}; hierarchy.clips={parentMotion};
        Require(Near(RootMotion::Delta(hierarchy,1,"Parent",0,.1,true),identity),"local root delta excludes animated ancestor");
        AnimatorComponent inherited; inherited.rootMotion=true; inherited.rootBone="root"; inherited.states[0].clip="Parent";
        AnimatorState inheritedState; Animator::Advance(inherited,inheritedState,hierarchy,.1,{});
        const auto normalized=RootMotion::PoseFrame(hierarchy,inheritedState.pose,1);
        Require(Near(normalized,RootMotion::Sample(hierarchy,1,"",0,false,true)),"global root normalization includes animated ancestor position and rotation");
        const auto raw=Skeleton::Matrices(hierarchy,Skeleton::Sample(hierarchy,"Parent",.1,true));
        auto inPlace=Skeleton::Matrices(hierarchy,inheritedState.pose);
        const auto inheritedDelta=Animator::RootDeltaMatrix(inherited,inheritedState,hierarchy);
        XMFLOAT4X4 reconstructed; XMStoreFloat4x4(&reconstructed,XMLoadFloat4x4(&inPlace[1])*XMLoadFloat4x4(&inheritedDelta));
        Require(Near(reconstructed,raw[1]) && std::abs(inheritedState.pose[0].scale[0]-1.1f)<.0001f,"actor delta reconstructs animated ancestor geometry while retaining scale");
    }
}
