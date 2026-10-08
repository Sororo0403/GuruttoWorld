#pragma once
#include <Engine/Animation/TwoBoneIk.h>
#include <SceneRuntime/Animator.h>
#include "../SceneRuntime/src/AnimatorJson.h"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace IkValidation
{
    inline void Require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
    inline std::array<float,3> Position(const Engine::SkeletonData& rig,const std::vector<Engine::BonePose>& pose,size_t node)
    {
        const auto matrix=Engine::Skeleton::Matrices(rig,pose).at(node);
        return {matrix._41,matrix._42,matrix._43};
    }
    inline float Distance(const std::array<float,3>& a,const std::array<float,3>& b)
    {
        float sum=0; for (size_t axis=0;axis<3;++axis) sum+=(a[axis]-b[axis])*(a[axis]-b[axis]); return std::sqrt(sum);
    }
    inline void Run()
    {
        using namespace Engine;
        SkeletonData rig; rig.nodes={{"parent",-1,{}},{"root",0,{}},{"middle",1,{}},{"tip",2,{}}};
        rig.nodes[2].rest.position={1,0,0}; rig.nodes[3].rest.position={1,0,0};
        auto pose=Skeleton::Sample(rig,"",0,false);
        TwoBoneIkConstraint ik; ik.root=1; ik.middle=2; ik.tip=3; ik.target={1,1,0}; ik.hint={0,1,0};
        const auto solved=TwoBoneIk::Solve(rig,pose,ik);
        Require(Distance(Position(rig,solved,3),ik.target)<0.0001f,"IK reaches target");
        Require(Position(rig,solved,2)[1]>0.99f,"IK bend follows hint");
        Require(solved[3].rotation==pose[3].rotation && solved[2].position==pose[2].position && solved[1].scale==pose[1].scale,"IK preserves bone lengths and tip rotation");
        ik.hint={2,0,0}; const auto opposite=TwoBoneIk::Solve(rig,pose,ik);
        Require(Position(rig,opposite,2)[0]>0.99f && Distance(Position(rig,opposite,3),ik.target)<0.0001f,"IK opposite bend reaches same target");
        ik.target={-5,0,0}; ik.hint={0,0,0};
        const auto distant=TwoBoneIk::Solve(rig,pose,ik);
        Require(Distance(Position(rig,distant,3),{-2,0,0})<0.0001f,"IK clamps distant target and handles antiparallel rotation");
        ik.target={0,0,0}; const auto folded=TwoBoneIk::Solve(rig,pose,ik);
        Require(Distance(Position(rig,folded,3),ik.target)<0.0001f,"IK folds to coincident target");
        ik.target={1,1,0}; ik.weight=0; const auto unchanged=TwoBoneIk::Solve(rig,pose,ik);
        Require(unchanged[1].rotation==pose[1].rotation && unchanged[2].rotation==pose[2].rotation,"IK zero weight leaves pose exact");
        ik.weight=0.5f; const auto partial=TwoBoneIk::Solve(rig,pose,ik);
        Require(Distance(Position(rig,partial,3),ik.target)>0.01f && partial[2].rotation!=pose[2].rotation,"IK fractional weight blends rotations");
        auto nonUnit=pose; nonUnit[0].rotation={0,0,0,2}; nonUnit[3].rotation={0,0,0,3};
        const auto partialPreserved=TwoBoneIk::Solve(rig,nonUnit,ik);
        Require(partialPreserved[0].rotation==nonUnit[0].rotation && partialPreserved[3].rotation==nonUnit[3].rotation,"IK fractional weight preserves unrelated and tip rotations exactly");
        ik.weight=1;
        pose[0].position={3,-2,4}; pose[0].scale={2,2,2};
        pose[0].rotation={0,0,std::sin(0.4f),std::cos(0.4f)};
        ik.target={4,-1,5}; ik.hint={3,-2,7};
        const auto transformed=TwoBoneIk::Solve(rig,pose,ik);
        Require(Distance(Position(rig,transformed,3),ik.target)<0.0002f,"IK handles rotated scaled ancestor and 3D target");
        for (int index=1;index<=120;++index)
        {
            const float angle=index*.137f;
            ik.target={3+std::cos(angle)*2,-2+std::sin(angle)*1.7f,4+std::sin(angle*.7f)};
            ik.hint={3+std::sin(angle),-2+std::cos(angle),6};
            const auto sweep=TwoBoneIk::Solve(rig,pose,ik);
            Require(Distance(Position(rig,sweep,3),ik.target)<0.0005f,"IK 3D sweep reaches target");
            Require(std::abs(Distance(Position(rig,sweep,1),Position(rig,sweep,2))-2)<0.0005f &&
                std::abs(Distance(Position(rig,sweep,2),Position(rig,sweep,3))-2)<0.0005f,"IK sweep retains segment lengths");
        }
        pose[3].position={0.5f,0,0}; ik.target=pose[0].position;
        const auto shortReach=TwoBoneIk::Solve(rig,pose,ik);
        Require(std::abs(Distance(Position(rig,shortReach,3),Position(rig,shortReach,1))-1)<0.0002f,"IK clamps target inside minimum reach");
        auto Reject=[&](const auto& invalidPose,const auto& invalidIk) {
            bool rejected=false; try { TwoBoneIk::Solve(rig,invalidPose,invalidIk); } catch (const std::exception&) { rejected=true; }
            Require(rejected,"IK rejects invalid input");
        };
        auto invalid=ik; invalid.tip=0; Reject(pose,invalid);
        invalid=ik; invalid.weight=std::numeric_limits<float>::quiet_NaN(); Reject(pose,invalid);
        invalid=ik; invalid.target[0]=std::numeric_limits<float>::infinity(); Reject(pose,invalid);
        auto badPose=pose; badPose[0].scale={1,2,1}; Reject(badPose,ik);
        badPose=pose; badPose[2].position={0,0,0}; Reject(badPose,ik);
        badPose=pose; badPose.pop_back(); Reject(badPose,ik);
        badPose=pose; badPose[1].rotation={0,0,0,0}; Reject(badPose,ik);
        using namespace SceneRuntime;
        rig.nodes[0].rest={}; rig.nodes[3].rest.position={1,0,0};
        rig.clips.push_back({"Idle",1,{}});
        AnimatorComponent animator; animator.states[0].clip="Idle";
        AnimatorIkConstraint definition; definition.name="arm"; definition.root="root"; definition.middle="middle"; definition.tip="tip";
        definition.target={1,1,0}; definition.hint={0,1,0}; animator.ik.push_back(definition);
        const auto json=WriteAnimator(animator); const auto restored=ReadAnimator(json,animator.id,animator.enabled);
        Require(restored==animator,"IK Animator JSON roundtrip");
        AnimatorState state; Animator::Advance(animator,state,rig,.1,{});
        Require(Distance(Position(rig,state.pose,3),definition.target)<.0001f && Distance(Position(rig,state.basePose,3),{2,0,0})<.0001f,"Animator applies IK after sampling and preserves base pose");
        for (int index=0;index<10;++index) Animator::Advance(animator,state,rig,.1,{});
        Require(Distance(Position(rig,state.pose,3),definition.target)<.0001f,"Animator IK does not accumulate across frames");
        const auto oldTime=state.time; const auto oldRotation=state.pose[1].rotation;
        animator.enabled=false; Animator::Advance(animator,state,rig,.1,{});
        Require(state.time==oldTime && state.pose[1].rotation==oldRotation,"disabled Animator freezes IK pose");
        animator.enabled=true; animator.ik[0].target={-1,1,0}; Animator::Advance(animator,state,rig,.1,{});
        Require(Distance(Position(rig,state.pose,3),animator.ik[0].target)<.0001f,"Animator follows changed IK target");
        const auto successfulTime=state.time; const auto successfulRotation=state.pose[1].rotation;
        animator.ik[0].middle="missing"; bool failed=false;
        try { Animator::Advance(animator,state,rig,.1,{}); } catch (const std::exception&) { failed=true; }
        Require(failed && state.time==successfulTime && state.pose[1].rotation==successfulRotation,"failed IK preserves Animator clock and pose");
        animator.ik[0]=definition;
        animator.states.push_back({"Next","Idle",1,true}); animator.transitions.push_back({"Idle","Next","go",">",0,.5f,-1});
        AnimatorState transitioning; Animator::Advance(animator,transitioning,rig,.1,{});
        Animator::Advance(animator,transitioning,rig,.1,{{"go",1.0f}});
        Require(transitioning.current=="Next" && Distance(Position(rig,transitioning.basePose,3),{2,0,0})<.0001f &&
            Distance(Position(rig,transitioning.pose,3),definition.target)<.0001f,"Animator crossfade uses uncorrected source before IK");
        auto invalidJson=WriteAnimator(animator); invalidJson["ik"][0]["target"]=Engine::Json::array({1,2});
        bool badJson=false; try { ReadAnimator(invalidJson,animator.id,true); } catch (const std::exception&) { badJson=true; }
        Require(badJson,"IK rejects malformed saved point");
        animator.ik.push_back(definition); bool duplicate=false;
        try { Animator::Validate(animator,&rig); } catch (const std::exception&) { duplicate=true; }
        Require(duplicate,"IK rejects duplicate constraint names");
        animator.ik.resize(1); animator.ik[0].worldSpace=true;
        rig.importScale=2;
        DirectX::XMStoreFloat4x4(&rig.inverseRoot,DirectX::XMMatrixTranslation(0,0,2));
        DirectX::XMFLOAT4X4 world;
        DirectX::XMStoreFloat4x4(&world,DirectX::XMMatrixScaling(2,3,1)*DirectX::XMMatrixRotationY(.6f)*DirectX::XMMatrixTranslation(3,4,5));
        const auto skeletonWorld=DirectX::XMLoadFloat4x4(&rig.inverseRoot)*DirectX::XMMatrixScaling(2,2,2)*DirectX::XMLoadFloat4x4(&world);
        const auto Transform=[&](const std::array<float,3>& point) {
            DirectX::XMFLOAT3 value;
            DirectX::XMStoreFloat3(&value,DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(point[0],point[1],point[2],1),skeletonWorld));
            return std::array<float,3>{value.x,value.y,value.z};
        };
        animator.ik[0].target=Transform(definition.target); animator.ik[0].hint=Transform(definition.hint);
        AnimatorState worldState; Animator::Advance(animator,worldState,rig,.1,{},&world);
        Require(Distance(Transform(Position(rig,worldState.pose,3)),animator.ik[0].target)<.0005f,"world IK accounts for import root, import scale and full object transform");
        const auto worldTime=worldState.time; bool missingWorld=false;
        try { Animator::Advance(animator,worldState,rig,.1,{}); } catch (const std::exception&) { missingWorld=true; }
        Require(missingWorld && worldState.time==worldTime,"world IK missing transform preserves clock");
        auto projective=world; projective._14=.5f; bool badWorld=false;
        try { Animator::Advance(animator,worldState,rig,.1,{},&projective); } catch (const std::exception&) { badWorld=true; }
        Require(badWorld && worldState.time==worldTime,"world IK rejects projective transform transactionally");
        Require(ReadAnimator(WriteAnimator(animator),animator.id,true)==animator,"world IK setting roundtrip");
    }
}
