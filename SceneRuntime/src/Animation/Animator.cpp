#include <Engine/Core/Validation.h>
#include <SceneRuntime/Animation/Animator.h>
#include "AnimatorValidation.h"
#include <Engine/Core/Profiler.h>
#include <Engine/Animation/TwoBoneIk.h>
#include <Engine/Animation/RootMotion.h>
#include <SceneRuntime/Scene/SceneTransforms.h>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace
{
    using SceneRuntime::FindAnimatorBone;
    Engine::BonePose RootDelta(const Engine::SkeletonData& rig, size_t bone,
                               const std::vector<SceneRuntime::AnimatorMotionSample>& motions, double from, double to,
                               bool loop)
    {
        std::vector<Engine::BonePose> blended;
        double total = 0;
        for (const auto& motion : motions)
            if (motion.weight > 0)
            {
                const auto matrix = Engine::RootMotion::Delta(rig, bone, motion.clip, from, to, loop, true);
                DirectX::XMVECTOR scale, rotation, position;
                if (!DirectX::XMMatrixDecompose(&scale, &rotation, &position, DirectX::XMLoadFloat4x4(&matrix)))
                    throw std::runtime_error("Invalid root motion delta");
                DirectX::XMFLOAT3 translation;
                DirectX::XMFLOAT4 quaternion;
                DirectX::XMStoreFloat3(&translation, position);
                DirectX::XMStoreFloat4(&quaternion, DirectX::XMQuaternionNormalize(rotation));
                Engine::BonePose pose;
                pose.position = {translation.x, translation.y, translation.z};
                pose.rotation = {quaternion.x, quaternion.y, quaternion.z, quaternion.w};
                if (blended.empty())
                    blended = {pose};
                else
                    blended = Engine::Skeleton::Blend(blended, {pose},
                                                      static_cast<float>(motion.weight / (total + motion.weight)));
                total += motion.weight;
            }
        if (blended.empty())
            throw std::runtime_error("Root motion weights empty");
        return blended.front();
    }

    std::vector<Engine::BonePose> SolveIk(const SceneRuntime::AnimatorComponent& component,
                                          const Engine::SkeletonData& rig, std::vector<Engine::BonePose> pose,
                                          const DirectX::XMFLOAT4X4* modelWorld)
    {
        for (const auto& item : component.ik)
            if (item.enabled && item.weight > 0)
            {
                Engine::TwoBoneIkConstraint constraint;
                constraint.root = FindAnimatorBone(rig, item.root);
                constraint.middle = FindAnimatorBone(rig, item.middle);
                constraint.tip = FindAnimatorBone(rig, item.tip);
                constraint.target = item.target;
                constraint.hint = item.hint;
                constraint.weight = item.weight;
                if (item.worldSpace)
                {
                    using namespace DirectX;
                    if (!modelWorld || !std::isfinite(rig.importScale) || rig.importScale <= 0)
                        throw std::runtime_error("World-space IK requires model transform");
                    const auto skeletonWorld = XMLoadFloat4x4(&rig.inverseRoot) *
                                               XMMatrixScaling(rig.importScale, rig.importScale, rig.importScale) *
                                               XMLoadFloat4x4(modelWorld);
                    XMFLOAT4X4 transform;
                    XMStoreFloat4x4(&transform, skeletonWorld);
                    if (!SceneRuntime::SceneTransforms::IsUsable(transform))
                        throw std::runtime_error("Invalid world-space IK transform");
                    const auto inverse = XMMatrixInverse(nullptr, skeletonWorld);
                    for (auto* point : {&constraint.target, &constraint.hint})
                    {
                        XMFLOAT3 converted;
                        XMStoreFloat3(&converted, XMVector3TransformCoord(
                                                      XMVectorSet((*point)[0], (*point)[1], (*point)[2], 1), inverse));
                        *point = {converted.x, converted.y, converted.z};
                    }
                }
                pose = Engine::TwoBoneIk::Solve(rig, pose, constraint);
            }
        return pose;
    }

    const SceneRuntime::AnimatorStateDefinition& State(const SceneRuntime::AnimatorComponent& component,
                                                       const std::string& name)
    {
        const auto found = std::find_if(component.states.begin(), component.states.end(),
                                        [&](const auto& state)
                                        {
                                            return state.name == name;
                                        });
        if (found == component.states.end())
            throw std::runtime_error("Animator state missing: " + name);
        return *found;
    }
    bool Condition(const SceneRuntime::AnimatorTransition& transition, const std::map<std::string, float>& parameters)
    {
        if (transition.parameter.empty())
            return true;
        const auto found = parameters.find(transition.parameter);
        const float value = found == parameters.end() ? 0 : found->second;
        if (!std::isfinite(value))
            throw std::runtime_error("Nonfinite Animator parameter");
        if (transition.comparison == ">")
            return value > transition.value;
        if (transition.comparison == ">=")
            return value >= transition.value;
        if (transition.comparison == "<")
            return value < transition.value;
        if (transition.comparison == "<=")
            return value <= transition.value;
        if (transition.comparison == "==")
            return value == transition.value;
        return value != transition.value;
    }
    std::vector<SceneRuntime::AnimatorMotionSample> Motions(const SceneRuntime::AnimatorComponent& component,
                                                            const SceneRuntime::AnimatorStateDefinition& definition,
                                                            const std::map<std::string, float>& parameters)
    {
        return definition.blendTree.empty()
                   ? std::vector<SceneRuntime::AnimatorMotionSample>{{definition.clip, 1, 1}}
                   : SceneRuntime::BlendTree::Samples(component.blendTrees, definition.blendTree, parameters);
    }
    std::vector<Engine::BonePose> Sample(const SceneRuntime::AnimatorComponent& component,
                                         const SceneRuntime::AnimatorStateDefinition& definition,
                                         const SceneRuntime::AnimatorState& state, const Engine::SkeletonData& rig,
                                         const std::map<std::string, float>& parameters)
    {
        return definition.blendTree.empty()
                   ? Engine::Skeleton::Sample(rig, definition.clip, state.time, definition.loop)
                   : SceneRuntime::BlendTree::SamplePose(Motions(component, definition, parameters), rig,
                                                         state.normalizedTime, definition.loop);
    }
    void StripRoot(const SceneRuntime::AnimatorComponent& component, const Engine::SkeletonData& rig,
                   std::vector<Engine::BonePose>& pose)
    {
        if (!component.rootMotion)
            return;
        using namespace DirectX;
        const auto bone = FindAnimatorBone(rig, component.rootBone);
        const auto current = Engine::RootMotion::PoseFrame(rig, pose, bone),
                   rest = Engine::RootMotion::Sample(rig, bone, "", 0, false, true);
        const auto normalization = XMMatrixInverse(nullptr, XMLoadFloat4x4(&current)) * XMLoadFloat4x4(&rest);
        XMVECTOR scale, rotation, position;
        if (!XMMatrixDecompose(&scale, &rotation, &position, normalization))
            throw std::runtime_error("Invalid root normalization");
        for (size_t index = 0; index < pose.size(); ++index)
            if (rig.nodes[index].parent == -1)
            {
                auto& value = pose[index];
                XMFLOAT3 translated;
                XMFLOAT4 oriented;
                XMStoreFloat3(&translated, XMVector3TransformCoord(
                                               XMVectorSet(value.position[0], value.position[1], value.position[2], 1),
                                               normalization));
                XMStoreFloat4(&oriented, XMQuaternionNormalize(
                                             XMQuaternionMultiply(XMVectorSet(value.rotation[0], value.rotation[1],
                                                                              value.rotation[2], value.rotation[3]),
                                                                  rotation)));
                value.position = {translated.x, translated.y, translated.z};
                value.rotation = {oriented.x, oriented.y, oriented.z, oriented.w};
            }
        if (rig.nodes[bone].parent == -1)
        {
            pose[bone].position = rig.nodes[bone].rest.position;
            pose[bone].rotation = rig.nodes[bone].rest.rotation;
        }
    }

    void ApplyIkOverrides(SceneRuntime::AnimatorComponent& effective, const SceneRuntime::AnimatorState& next)
    {
        if (next.ikOverrides.size() > 16)
            throw std::runtime_error("Too many IK overrides");
        for (const auto& [name, target] : next.ikOverrides)
        {
            SceneRuntime::Animator::ValidateIkTarget(target);
            const auto found = std::ranges::find_if(effective.ik,
                                                    [&](const auto& item)
                                                    {
                                                        return item.name == name;
                                                    });
            if (found == effective.ik.end())
                throw std::runtime_error("IK override constraint missing: " + name);
            found->target = target.target;
            found->hint = target.hint;
            found->weight = target.weight;
            found->worldSpace = target.worldSpace;
        }
    }

    void ApplyTransition(const SceneRuntime::AnimatorComponent& component, SceneRuntime::AnimatorState& next,
                         const Engine::SkeletonData& rig, const SceneRuntime::AnimatorStateDefinition& before,
                         const std::map<std::string, float>& values, bool initializing, double duration)
    {
        for (const auto& transition : component.transitions)
        {
            const double phase = before.blendTree.empty() ? next.time / duration : next.normalizedTime;
            if (initializing || (transition.from != "*" && transition.from != next.current) ||
                transition.to == next.current || (transition.exitTime >= 0 && phase < transition.exitTime) ||
                !Condition(transition, values))
                continue;
            next.previousPose = next.basePose.empty() ? Sample(component, before, next, rig, values) : next.basePose;
            StripRoot(component, rig, next.previousPose);
            if (component.rootMotion)
            {
                next.previousRootMotions = Motions(component, before, values);
                next.previousRootPhase = phase;
                next.previousRootSpeed = before.speed;
                next.previousRootLoop = before.loop;
            }
            next.current = transition.to;
            next.time = next.normalizedTime = 0;
            next.blendElapsed = 0;
            next.blendDuration = transition.blendSeconds;
            next.eventsAtStart = true;
            break;
        }
    }

    void UpdateRootDelta(const SceneRuntime::AnimatorComponent& component, SceneRuntime::AnimatorState& next,
                         const Engine::SkeletonData& rig, const SceneRuntime::AnimatorStateDefinition& definition,
                         double from, float tick, std::array<float, 2> weights, std::vector<Engine::BonePose>& pose)
    {
        const auto [fromWeight, toWeight] = weights;
        if (component.rootMotion)
        {
            const auto bone = FindAnimatorBone(rig, component.rootBone);
            next.rootDelta = RootDelta(rig, bone, next.motions, from, next.normalizedTime, definition.loop);
            if (!next.previousRootMotions.empty() && next.blendDuration > 0 && fromWeight < 1)
            {
                const double previousTo =
                    next.previousRootPhase +
                    tick * next.previousRootSpeed / SceneRuntime::BlendTree::Duration(next.previousRootMotions, rig);
                const auto outgoing = RootDelta(rig, bone, next.previousRootMotions, next.previousRootPhase, previousTo,
                                                next.previousRootLoop);
                next.previousRootPhase = previousTo;
                next.rootDelta =
                    Engine::Skeleton::Blend({outgoing}, {next.rootDelta}, (fromWeight + toWeight) * .5f).front();
            }
            StripRoot(component, rig, pose);
        }
    }

    float BlendWeight(const SceneRuntime::AnimatorState& state)
    {
        return state.previousPose.empty() || state.blendDuration <= 0
                   ? 1
                   : std::clamp(state.blendElapsed / state.blendDuration, 0.0f, 1.0f);
    }

    void BlendPreviousPose(SceneRuntime::AnimatorState& next, float tick, std::vector<Engine::BonePose>& pose)
    {
        if (!next.previousPose.empty() && next.blendDuration > 0)
        {
            next.blendElapsed = std::min(next.blendElapsed + tick, next.blendDuration);
            pose = Engine::Skeleton::Blend(next.previousPose, pose, next.blendElapsed / next.blendDuration);
            if (next.blendElapsed >= next.blendDuration)
                next.previousPose.clear();
        }
        else
            next.previousPose.clear();
    }

    void ValidateClock(const SceneRuntime::AnimatorState& next)
    {
        if (!std::isfinite(next.time) || next.time < 0 || !std::isfinite(next.normalizedTime) ||
            next.normalizedTime < 0)
            throw std::runtime_error("Invalid Animator clock");
    }

}
namespace SceneRuntime
{
    DirectX::XMFLOAT4X4 Animator::RootDeltaMatrix(const AnimatorComponent& component, const AnimatorState& state,
                                                  const Engine::SkeletonData& rig)
    {
        using namespace DirectX;
        XMFLOAT4X4 result;
        XMStoreFloat4x4(&result, XMMatrixIdentity());
        if (!state.rootMotionOverride.value_or(component.rootMotion) || !component.enabled ||
            (state.rootDelta.position == std::array<float, 3>{} &&
             state.rootDelta.rotation == std::array<float, 4>{0, 0, 0, 1}))
            return result;
        const auto bone = FindAnimatorBone(rig, component.rootBone);
        const auto rest = Engine::RootMotion::Sample(rig, bone, "", 0, false, true);
        auto reference = XMLoadFloat4x4(&rest);
        if (!std::isfinite(rig.importScale) || rig.importScale <= 0)
            throw std::runtime_error("Invalid root motion import scale");
        reference = reference * XMLoadFloat4x4(&rig.inverseRoot) *
                    XMMatrixScaling(rig.importScale, rig.importScale, rig.importScale);
        XMFLOAT4X4 referenceMatrix;
        XMStoreFloat4x4(&referenceMatrix, reference);
        if (!SceneTransforms::IsUsable(referenceMatrix))
            throw std::runtime_error("Invalid root motion reference transform");
        const auto& rotation = state.rootDelta.rotation;
        const auto& position = state.rootDelta.position;
        const auto quaternion = XMVectorSet(rotation[0], rotation[1], rotation[2], rotation[3]);
        const float norm = XMVectorGetX(XMVector4LengthSq(quaternion));
        if (!std::isfinite(norm) || norm < 1e-12f)
            throw std::runtime_error("Invalid root motion delta rotation");
        const auto delta = XMMatrixRotationQuaternion(XMQuaternionNormalize(quaternion)) *
                           XMMatrixTranslation(position[0], position[1], position[2]);
        XMStoreFloat4x4(&result, XMMatrixInverse(nullptr, reference) * delta * reference);
        if (!SceneTransforms::IsUsable(result))
            throw std::runtime_error("Invalid root motion model delta");
        return result;
    }
    std::vector<Engine::BonePose> Animator::Constrain(const AnimatorComponent& component, const AnimatorState& state,
                                                      const Engine::SkeletonData& rig,
                                                      const DirectX::XMFLOAT4X4* modelWorld)
    {
        Validate(component, &rig);
        if (state.basePose.size() != rig.nodes.size() || state.ikOverrides.size() > 16)
            throw std::runtime_error("Invalid constraint pose");
        auto effective = component;
        for (const auto& [name, target] : state.ikOverrides)
        {
            ValidateIkTarget(target);
            const auto found = std::ranges::find_if(effective.ik,
                                                    [&](const auto& item)
                                                    {
                                                        return item.name == name;
                                                    });
            if (found == effective.ik.end())
                throw std::runtime_error("IK override constraint missing: " + name);
            found->target = target.target;
            found->hint = target.hint;
            found->weight = target.weight;
            found->worldSpace = target.worldSpace;
        }
        return SolveIk(effective, rig, state.basePose, modelWorld);
    }
    void Animator::ValidateIkTarget(const AnimatorIkTarget& target)
    {
        if (!Engine::InRange(target.weight, 0, 1))
            throw std::runtime_error("Invalid IK target weight");
        for (const auto& point : {target.target, target.hint})
            if (std::ranges::any_of(point,
                                    [](float value)
                                    {
                                        return !Engine::InRange(value, -1000000, 1000000);
                                    }))
                throw std::runtime_error("Invalid IK target point");
    }

    std::vector<Engine::BonePose> Animator::Advance(const AnimatorComponent& authored, AnimatorState& state,
                                                    const Engine::SkeletonData& rig, double seconds,
                                                    const std::map<std::string, float>& parameters,
                                                    const DirectX::XMFLOAT4X4* modelWorld)
    {
        if (!std::isfinite(seconds) || seconds < 0)
            throw std::runtime_error("Invalid Animator time");
        Engine::CpuScope scope("Animator evaluation");
        auto component = authored;
        component.rootMotion = state.rootMotionOverride.value_or(authored.rootMotion);
        Validate(component, &rig);
        auto next = state;
        next.rootDelta = {};
        auto effective = component;
        ApplyIkOverrides(effective, next);
        next.events.clear();
        ValidateParameters(next.parameterOverrides);
        auto values = component.parameters;
        for (const auto& [name, value] : parameters)
            values[name] = value;
        for (const auto& [name, value] : next.parameterOverrides)
            values[name] = value;
        const bool initializing = next.current.empty();
        if (initializing)
            next.current = component.initialState;
        ValidateClock(next);
        const auto& before = State(component, next.current);

        if (!component.enabled)
        {
            if (next.pose.empty())
            {
                next.basePose = Sample(component, before, next, rig, values);
                StripRoot(component, rig, next.basePose);
                next.pose = SolveIk(effective, rig, next.basePose, modelWorld);
                next.motions = Motions(component, before, values);
            }
            auto pose = next.pose;
            state = std::move(next);
            return pose;
        }
        const double duration = BlendTree::Duration(Motions(component, before, values), rig);
        ApplyTransition(component, next, rig, before, values, initializing, duration);
        const auto& definition = State(component, next.current);
        next.motions = Motions(component, definition, values);
        const double cycleDuration = BlendTree::Duration(next.motions, rig);
        const double from = definition.blendTree.empty() ? next.time / cycleDuration : next.normalizedTime;
        const float fromWeight = BlendWeight(next);
        const float tick = static_cast<float>(std::min(seconds, 0.1));
        next.time += tick * definition.speed;
        next.normalizedTime = definition.blendTree.empty()
                                  ? next.time / cycleDuration
                                  : next.normalizedTime + tick * definition.speed / cycleDuration;
        auto pose = definition.blendTree.empty()
                        ? Engine::Skeleton::Sample(rig, definition.clip, next.time, definition.loop)
                        : BlendTree::SamplePose(next.motions, rig, next.normalizedTime, definition.loop);
        BlendPreviousPose(next, tick, pose);
        const float toWeight = BlendWeight(next);
        next.events = AnimationEvents::Collect(
            component.events, rig, next.current, next.motions,
            {from, next.normalizedTime, definition.loop, next.eventsAtStart, fromWeight, toWeight});
        if (next.normalizedTime > from)
            next.eventsAtStart = false;
        UpdateRootDelta(component, next, rig, definition, from, tick, {fromWeight, toWeight}, pose);
        if (!component.rootMotion || toWeight >= 1)
            next.previousRootMotions.clear();
        next.basePose = pose;
        pose = SolveIk(effective, rig, std::move(pose), modelWorld);
        next.pose = pose;
        state = std::move(next);
        return pose;
    }
}
