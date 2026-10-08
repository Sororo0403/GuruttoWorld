#pragma once
#include <Engine/Animation/TwoBoneIk.h>
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
    }
}
