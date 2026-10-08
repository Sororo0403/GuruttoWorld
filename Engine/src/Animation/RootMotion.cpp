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
    XMMATRIX Frame(const Engine::SkeletonData& rig,const std::vector<Engine::BonePose>& pose,size_t bone,bool global)
    {
        const auto& value=pose.at(bone);
        const auto local=Rigid(value); if (!global) return local;
        auto matrix=XMMatrixScaling(value.scale[0],value.scale[1],value.scale[2])*local;
        auto rotation=XMQuaternionNormalize(XMVectorSet(value.rotation[0],value.rotation[1],value.rotation[2],value.rotation[3]));
        size_t node=bone; int parent=rig.nodes[node].parent;
        while (parent>=0)
        {
            if (static_cast<size_t>(parent)>=node) throw std::runtime_error("Invalid root motion hierarchy");
            node=static_cast<size_t>(parent); const auto& ancestor=pose.at(node);
            matrix=matrix*XMMatrixScaling(ancestor.scale[0],ancestor.scale[1],ancestor.scale[2])*Rigid(ancestor);
            rotation=XMQuaternionNormalize(XMQuaternionMultiply(rotation,XMQuaternionNormalize(XMVectorSet(ancestor.rotation[0],ancestor.rotation[1],ancestor.rotation[2],ancestor.rotation[3]))));
            parent=rig.nodes[node].parent;
        }
        if (parent<-1) throw std::runtime_error("Invalid root motion hierarchy");
        const auto checked=Store(matrix);
        auto result=XMMatrixRotationQuaternion(rotation); result.r[3]=XMVectorSet(checked._41,checked._42,checked._43,1); return result;
    }
}
namespace Engine
{
    DirectX::XMFLOAT4X4 RootMotion::PoseFrame(const SkeletonData& rig,const std::vector<BonePose>& pose,size_t bone,bool global)
    {
        if (bone>=rig.nodes.size() || pose.size()!=rig.nodes.size()) throw std::runtime_error("Invalid root motion pose size");
        return Store(Frame(rig,pose,bone,global));
    }
    DirectX::XMFLOAT4X4 RootMotion::Sample(const SkeletonData& rig,size_t bone,const std::string& name,double phase,bool loop,bool global)
    {
        ValidatePhase(phase);
        if (bone>=rig.nodes.size()) throw std::runtime_error("Invalid root motion bone");
        if (name.empty()) return Store(Frame(rig,Skeleton::Sample(rig,"",0,false),bone,global));
        const auto clip=std::ranges::find_if(rig.clips,[&](const auto& item) { return item.name==name; });
        if (clip==rig.clips.end() || !std::isfinite(clip->duration) || clip->duration<=0) throw std::runtime_error("Invalid root motion clip");
        const double cycles=loop ? std::floor(phase) : 0;
        const double fraction=loop ? phase-cycles : std::min(phase,1.0);
        const auto sample=Frame(rig,Skeleton::Sample(rig,name,fraction*clip->duration,false),bone,global);
        if (cycles==0) return Store(sample);
        const auto start=Frame(rig,Skeleton::Sample(rig,name,0,false),bone,global);
        const auto end=Frame(rig,Skeleton::Sample(rig,name,clip->duration,false),bone,global);
        return Store(sample*Power(XMMatrixInverse(nullptr,start)*end,static_cast<uint64_t>(cycles)));
    }
    DirectX::XMFLOAT4X4 RootMotion::Delta(const SkeletonData& rig,size_t bone,const std::string& clip,double from,double to,bool loop,bool global)
    {
        if (to<from) throw std::runtime_error("Root motion interval reversed");
        ValidatePhase(from); ValidatePhase(to);
        const auto first=Sample(rig,bone,clip,loop ? from-std::floor(from) : from,false,global);
        if (from==to) return Store(DirectX::XMMatrixIdentity());
        const auto second=Sample(rig,bone,clip,loop ? to-std::floor(to) : to,false,global);
        auto cycles=DirectX::XMMatrixIdentity();
        if (loop && std::floor(to)>std::floor(from))
        {
            const auto start=Sample(rig,bone,clip,0,false,global),end=Sample(rig,bone,clip,1,false,global);
            cycles=Power(DirectX::XMMatrixInverse(nullptr,DirectX::XMLoadFloat4x4(&start))*DirectX::XMLoadFloat4x4(&end),static_cast<uint64_t>(std::floor(to)-std::floor(from)));
        }
        return Store(DirectX::XMLoadFloat4x4(&second)*cycles*DirectX::XMMatrixInverse(nullptr,DirectX::XMLoadFloat4x4(&first)));
    }
}
