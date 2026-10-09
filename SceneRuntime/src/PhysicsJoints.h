#pragma once
#include <SceneRuntime/JointComponent.h>
#pragma warning(push,0)
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#pragma warning(pop)

namespace SceneRuntime::PhysicsJoints
{
    /// <summary>検証済みの設定とWorldアンカーからJolt制約を生成します。</summary>
    inline JPH::Constraint* Create(const JointComponent& joint,JPH::Body& first,JPH::Body& second,
        JPH::RVec3Arg point,JPH::RVec3Arg connected,JPH::Vec3Arg axis)
    {
        const auto basis=(std::abs(axis.GetX())<0.8f ? axis.Cross(JPH::Vec3::sAxisX()) : axis.Cross(JPH::Vec3::sAxisY())).Normalized();
        const float radians=JPH::JPH_PI/180;
        if(joint.type=="fixed")
        {
            JPH::FixedConstraintSettings settings; settings.mPoint1=point;settings.mPoint2=connected;
            return settings.Create(first,second);
        }
        if(joint.type=="distance")
        {
            JPH::DistanceConstraintSettings settings;settings.mPoint1=point;settings.mPoint2=connected;
            settings.mMinDistance=joint.minDistance;settings.mMaxDistance=joint.maxDistance;
            return settings.Create(first,second);
        }
        if(joint.type=="hinge")
        {
            JPH::HingeConstraintSettings settings;settings.mPoint1=point;settings.mPoint2=connected;
            settings.mHingeAxis1=settings.mHingeAxis2=axis;settings.mNormalAxis1=settings.mNormalAxis2=basis;
            settings.mLimitsMin=joint.minimum*radians;settings.mLimitsMax=joint.maximum*radians;
            return settings.Create(first,second);
        }
        JPH::SwingTwistConstraintSettings settings;settings.mPosition1=point;settings.mPosition2=connected;
        settings.mTwistAxis1=settings.mTwistAxis2=axis;settings.mPlaneAxis1=settings.mPlaneAxis2=basis;
        settings.mNormalHalfConeAngle=settings.mPlaneHalfConeAngle=joint.swing*radians;
        settings.mTwistMinAngle=joint.minimum*radians;settings.mTwistMaxAngle=joint.maximum*radians;
        return settings.Create(first,second);
    }
}
