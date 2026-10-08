#pragma once
#include <Engine/Animation/RootMotion.h>
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
    }
}
