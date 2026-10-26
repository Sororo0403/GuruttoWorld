#pragma once
#include <SceneRuntime/Physics/JointComponent.h>
#pragma warning(push, 0)
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#pragma warning(pop)

namespace SceneRuntime::PhysicsJoints
{
    /// <summary>検証済みの設定とワールドアンカーからJolt制約を生成します。</summary>
    JPH::Constraint* Create(const JointComponent& joint, JPH::Body& first, JPH::Body& second, JPH::RVec3Arg point,
                            JPH::RVec3Arg connected, JPH::Vec3Arg axis);
}
