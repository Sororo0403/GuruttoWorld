#include <Engine/Animation/RootMotion.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace
{
    using namespace DirectX;
    void ValidatePhase(double phase)
    {
        if (!std::isfinite(phase) || phase<0 || phase>=9007199254740992.0) throw std::runtime_error("Invalid root motion phase");
    }
    XMMATRIX Rigid(const Engine::BonePose& pose)
    {
        const auto& rotation=pose.rotation;
        const auto quaternion=XMVectorSet(rotation[0],rotation[1],rotation[2],rotation[3]);
        const float norm=XMVectorGetX(XMVector4LengthSq(quaternion));
        if (!std::isfinite(norm) || norm<1e-12f || std::ranges::any_of(pose.position,[](float value) { return !std::isfinite(value); }))
            throw std::runtime_error("Invalid root motion pose");
        return XMMatrixRotationQuaternion(XMQuaternionNormalize(quaternion))*XMMatrixTranslation(pose.position[0],pose.position[1],pose.position[2]);
    }
    XMMATRIX Power(XMMATRIX matrix,uint64_t exponent)
    {
        XMVECTOR scale,rotation,position;
        if (!XMMatrixDecompose(&scale,&rotation,&position,matrix)) throw std::runtime_error("Invalid root motion cycle");
        rotation=XMQuaternionNormalize(rotation);
        auto resultRotation=XMQuaternionIdentity(),resultPosition=XMVectorZero();
        while (exponent>0)
        {
            if (exponent&1)
            {
                resultPosition=XMVectorAdd(XMVector3Rotate(resultPosition,rotation),position);
                resultRotation=XMQuaternionNormalize(XMQuaternionMultiply(resultRotation,rotation));
            }
            exponent>>=1;
            if (exponent)
            {
                position=XMVectorAdd(XMVector3Rotate(position,rotation),position);
                rotation=XMQuaternionNormalize(XMQuaternionMultiply(rotation,rotation));
            }
        }
        auto result=XMMatrixRotationQuaternion(resultRotation);
        result.r[3]=XMVectorSetW(resultPosition,1); return result;
    }
    XMFLOAT4X4 Store(FXMMATRIX matrix)
    {
        XMFLOAT4X4 result; XMStoreFloat4x4(&result,matrix);
        if (std::ranges::any_of(result.m,[](const auto& row) { return std::ranges::any_of(row,[](float value) { return !std::isfinite(value); }); }))
            throw std::runtime_error("Nonfinite root motion transform");
        return result;
    }
}
namespace Engine
{
    DirectX::XMFLOAT4X4 RootMotion::Sample(const SkeletonData& rig,size_t bone,const std::string& name,double phase,bool loop)
    {
        ValidatePhase(phase);
        if (bone>=rig.nodes.size()) throw std::runtime_error("Invalid root motion bone");
        if (name.empty()) return Store(Rigid(rig.nodes[bone].rest));
        const auto clip=std::ranges::find_if(rig.clips,[&](const auto& item) { return item.name==name; });
        if (clip==rig.clips.end() || !std::isfinite(clip->duration) || clip->duration<=0) throw std::runtime_error("Invalid root motion clip");
        const double cycles=loop ? std::floor(phase) : 0;
        const double fraction=loop ? phase-cycles : std::min(phase,1.0);
        const auto sample=Rigid(Skeleton::Sample(rig,name,fraction*clip->duration,false).at(bone));
        if (cycles==0) return Store(sample);
        const auto start=Rigid(Skeleton::Sample(rig,name,0,false).at(bone));
        const auto end=Rigid(Skeleton::Sample(rig,name,clip->duration,false).at(bone));
        return Store(sample*Power(XMMatrixInverse(nullptr,start)*end,static_cast<uint64_t>(cycles)));
    }
    DirectX::XMFLOAT4X4 RootMotion::Delta(const SkeletonData& rig,size_t bone,const std::string& clip,double from,double to,bool loop)
    {
        if (to<from) throw std::runtime_error("Root motion interval reversed");
        ValidatePhase(from); ValidatePhase(to);
        const auto first=Sample(rig,bone,clip,loop ? from-std::floor(from) : from,false);
        if (from==to) return Store(DirectX::XMMatrixIdentity());
        const auto second=Sample(rig,bone,clip,loop ? to-std::floor(to) : to,false);
        auto cycles=DirectX::XMMatrixIdentity();
        if (loop && std::floor(to)>std::floor(from))
        {
            const auto start=Sample(rig,bone,clip,0,false),end=Sample(rig,bone,clip,1,false);
            cycles=Power(DirectX::XMMatrixInverse(nullptr,DirectX::XMLoadFloat4x4(&start))*DirectX::XMLoadFloat4x4(&end),static_cast<uint64_t>(std::floor(to)-std::floor(from)));
        }
        return Store(DirectX::XMLoadFloat4x4(&second)*cycles*DirectX::XMMatrixInverse(nullptr,DirectX::XMLoadFloat4x4(&first)));
    }
}
