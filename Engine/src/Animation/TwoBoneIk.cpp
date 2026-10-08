#include <Engine/Animation/TwoBoneIk.h>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace
{
    using namespace DirectX;
    XMVECTOR Point(const std::array<float,3>& value) { return XMVectorSet(value[0],value[1],value[2],0); }
    float Length(FXMVECTOR value) { return XMVectorGetX(XMVector3Length(value)); }
    XMVECTOR Position(const XMFLOAT4X4& matrix) { return XMVectorSet(matrix._41,matrix._42,matrix._43,0); }
    XMVECTOR Perpendicular(FXMVECTOR direction)
    {
        const auto axis=std::abs(XMVectorGetX(direction))<0.8f ? XMVectorSet(1,0,0,0) : XMVectorSet(0,1,0,0);
        return XMVector3Normalize(XMVector3Cross(direction,axis));
    }
    XMVECTOR Between(FXMVECTOR from,FXMVECTOR to)
    {
        const auto first=XMVector3Normalize(from),second=XMVector3Normalize(to);
        const float cosine=std::clamp(XMVectorGetX(XMVector3Dot(first,second)),-1.0f,1.0f);
        const auto cross=XMVector3Cross(first,second);
        const float sine=Length(cross);
        if (sine<1e-7f) return cosine<0 ? XMQuaternionRotationAxis(Perpendicular(first),XM_PI) : XMQuaternionIdentity();
        return XMQuaternionRotationAxis(XMVectorScale(cross,1/sine),std::atan2(sine,cosine));
    }
    void Rotate(const Engine::SkeletonData& rig,std::vector<Engine::BonePose>& pose,size_t bone,size_t child,FXMVECTOR desired)
    {
        const auto matrices=Engine::Skeleton::Matrices(rig,pose);
        const auto origin=Position(matrices[bone]);
        auto current=XMVectorSubtract(Position(matrices[child]),origin);
        auto target=XMVectorSubtract(desired,origin);
        const int parent=rig.nodes[bone].parent;
        if (parent>=0)
        {
            const auto inverse=XMMatrixInverse(nullptr,XMLoadFloat4x4(&matrices[static_cast<size_t>(parent)]));
            current=XMVector3TransformNormal(current,inverse); target=XMVector3TransformNormal(target,inverse);
        }
        if (Length(current)<1e-8f || Length(target)<1e-8f) throw std::runtime_error("Degenerate IK rotation");
        const auto& rotation=pose[bone].rotation;
        XMFLOAT4 solved;
        XMStoreFloat4(&solved,XMQuaternionNormalize(XMQuaternionMultiply(
            XMVectorSet(rotation[0],rotation[1],rotation[2],rotation[3]),Between(current,target))));
        pose[bone].rotation={solved.x,solved.y,solved.z,solved.w};
    }
}
namespace Engine
{
    std::vector<BonePose> TwoBoneIk::Solve(const SkeletonData& rig,const std::vector<BonePose>& pose,const TwoBoneIkConstraint& constraint)
    {
        const auto count=rig.nodes.size();
        if (pose.size()!=count || constraint.root>=count || constraint.middle>=count || constraint.tip>=count ||
            rig.nodes[constraint.middle].parent!=static_cast<int>(constraint.root) || rig.nodes[constraint.tip].parent!=static_cast<int>(constraint.middle) ||
            !std::isfinite(constraint.weight) || constraint.weight<0 || constraint.weight>1)
            throw std::runtime_error("Invalid two-bone IK chain or weight");
        for (const auto& point : {constraint.target,constraint.hint})
            if (std::ranges::any_of(point,[](float entry) { return !std::isfinite(entry) || std::abs(entry)>1000000; })) throw std::runtime_error("Invalid IK target or hint");
        const auto matrices=Skeleton::Matrices(rig,pose);
        for (int ancestor=static_cast<int>(constraint.tip);ancestor>=0;ancestor=rig.nodes[static_cast<size_t>(ancestor)].parent)
        {
            const auto& scale=pose[static_cast<size_t>(ancestor)].scale;
            const auto& rotation=pose[static_cast<size_t>(ancestor)].rotation;
            const float norm=std::inner_product(rotation.begin(),rotation.end(),rotation.begin(),0.0f);
            if (!std::isfinite(norm) || norm<1e-12f) throw std::runtime_error("Invalid IK ancestor rotation");
            if (scale[0]<=0 || std::abs(scale[1]-scale[0])>1e-5f*scale[0] || std::abs(scale[2]-scale[0])>1e-5f*scale[0])
                throw std::runtime_error("Two-bone IK requires positive uniform ancestor scales");
        }
        const auto origin=Position(matrices[constraint.root]),middle=Position(matrices[constraint.middle]),tip=Position(matrices[constraint.tip]);
        const float first=Length(XMVectorSubtract(middle,origin)),second=Length(XMVectorSubtract(tip,middle));
        if (!std::isfinite(first+second) || first<1e-6f || second<1e-6f) throw std::runtime_error("Degenerate two-bone IK lengths");
        if (constraint.weight==0) return pose;
        const auto offset=XMVectorSubtract(Point(constraint.target),origin);
        const float distance=Length(offset);
        auto direction=distance>1e-7f ? XMVectorScale(offset,1/distance) : XMVectorSubtract(tip,origin);
        if (Length(direction)<1e-7f) direction=XMVectorSubtract(middle,origin);
        direction=XMVector3Normalize(direction);
        auto bend=XMVectorSubtract(Point(constraint.hint),origin);
        bend=XMVectorSubtract(bend,XMVectorScale(direction,XMVectorGetX(XMVector3Dot(bend,direction))));
        if (Length(bend)<1e-7f)
        {
            bend=XMVectorSubtract(middle,origin);
            bend=XMVectorSubtract(bend,XMVectorScale(direction,XMVectorGetX(XMVector3Dot(bend,direction))));
            if (Length(bend)<1e-7f) bend=Perpendicular(direction);
        }
        bend=XMVector3Normalize(bend);
        const float reach=std::clamp(distance,std::abs(first-second),first+second);
        const float along=reach>1e-7f ? (first*first-second*second+reach*reach)/(2*reach) : 0;
        const float height=std::sqrt(std::max(0.0f,first*first-along*along));
        const auto solvedMiddle=XMVectorAdd(origin,XMVectorAdd(XMVectorScale(direction,along),XMVectorScale(bend,height)));
        const auto solvedTip=XMVectorAdd(origin,XMVectorScale(direction,reach));
        auto result=pose;
        Rotate(rig,result,constraint.root,constraint.middle,solvedMiddle);
        Rotate(rig,result,constraint.middle,constraint.tip,solvedTip);
        if (constraint.weight<1) for (const size_t bone : {constraint.root,constraint.middle})
        {
            const auto& original=pose[bone].rotation; const auto& solved=result[bone].rotation;
            XMFLOAT4 blended;
            XMStoreFloat4(&blended,XMQuaternionNormalize(XMQuaternionSlerp(
                XMVectorSet(original[0],original[1],original[2],original[3]),
                XMVectorSet(solved[0],solved[1],solved[2],solved[3]),constraint.weight)));
            result[bone].rotation={blended.x,blended.y,blended.z,blended.w};
        }
        Skeleton::Matrices(rig,result);
        return result;
    }
}
