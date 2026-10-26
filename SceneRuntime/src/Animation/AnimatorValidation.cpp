#include "AnimatorValidation.h"
#include <Engine/Core/Validation.h>
#include <algorithm>
#include <set>
#include <stdexcept>

namespace SceneRuntime
{
    size_t FindAnimatorBone(const Engine::SkeletonData& rig, const std::string& name)
    {
        size_t result = rig.nodes.size();
        for (size_t index = 0; index < rig.nodes.size(); ++index)
            if (rig.nodes[index].name == name)
            {
                if (result != rig.nodes.size())
                    throw std::runtime_error("Ambiguous IK bone: " + name);
                result = index;
            }
        if (result == rig.nodes.size())
            throw std::runtime_error("Missing IK bone: " + name);
        return result;
    }
}
namespace
{
    using SceneRuntime::FindAnimatorBone;
    void ValidateIk(const SceneRuntime::AnimatorIkConstraint& item, std::set<std::string>& ikNames,
                    const Engine::SkeletonData* rig)
    {
        for (const auto& name : {item.name, item.root, item.middle, item.tip})
            if (!Engine::ValidString(name, 256, true))
                throw std::runtime_error("Invalid IK name");
        if (!ikNames.insert(item.name).second || !Engine::InRange(item.weight, 0, 1))
            throw std::runtime_error("Invalid IK weight or duplicate name");
        for (const auto& point : {item.target, item.hint})
            if (std::ranges::any_of(point,
                                    [](float value)
                                    {
                                        return !Engine::InRange(value, -1000000, 1000000);
                                    }))
                throw std::runtime_error("Invalid IK point");
        if (item.root == item.middle || item.root == item.tip || item.middle == item.tip)
            throw std::runtime_error("Invalid IK chain");
        if (rig)
        {
            const auto root = FindAnimatorBone(*rig, item.root), middle = FindAnimatorBone(*rig, item.middle),
                       tip = FindAnimatorBone(*rig, item.tip);
            if (rig->nodes[middle].parent != static_cast<int>(root) ||
                rig->nodes[tip].parent != static_cast<int>(middle))
                throw std::runtime_error("IK bones must be directly connected");
        }
    }
    void ValidateState(const SceneRuntime::AnimatorStateDefinition& state, std::set<std::string>& names,
                       const SceneRuntime::AnimatorComponent& component, const Engine::SkeletonData* rig)
    {
        if (!Engine::ValidString(state.name, 128, true) || state.name == "*" || !names.insert(state.name).second ||
            !Engine::ValidString(state.clip, 256) || !Engine::ValidString(state.blendTree, 128) ||
            !Engine::InRange(state.speed, 0, 1000))
            throw std::runtime_error("Invalid Animator state");
        if (rig && !state.clip.empty() &&
            std::none_of(rig->clips.begin(), rig->clips.end(),
                         [&](const auto& clip)
                         {
                             return clip.name == state.clip;
                         }))
            throw std::runtime_error("Animator clip missing: " + state.clip);
        if (!state.blendTree.empty() && std::none_of(component.blendTrees.begin(), component.blendTrees.end(),
                                                     [&](const auto& tree)
                                                     {
                                                         return tree.name == state.blendTree;
                                                     }))
            throw std::runtime_error("Animator Blend Tree missing: " + state.blendTree);
    }
    void ValidateTransition(const SceneRuntime::AnimatorTransition& transition, const std::set<std::string>& names)
    {
        const std::set<std::string> comparisons{">", ">=", "<", "<=", "==", "!="};
        if ((transition.from != "*" && !names.contains(transition.from)) || !names.contains(transition.to) ||
            !comparisons.contains(transition.comparison) || !Engine::ValidString(transition.parameter, 128) ||
            !Engine::InRange(transition.value, -1000000, 1000000) || !Engine::InRange(transition.blendSeconds, 0, 60) ||
            !Engine::InRange(transition.exitTime, -1, 100))
            throw std::runtime_error("Invalid Animator transition");
    }
}
namespace SceneRuntime
{
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

    void Animator::ValidateParameters(const std::map<std::string, float>& parameters)
    {
        if (parameters.size() > 64)
            throw std::runtime_error("Too many Animator parameters");
        for (const auto& [name, value] : parameters)
            if (name.empty() || name.size() > 128 || name.find('\0') != std::string::npos ||
                !Engine::InRange(value, -1000000, 1000000))
                throw std::runtime_error("Invalid Animator parameter");
    }
    void Animator::Validate(const AnimatorComponent& component, const Engine::SkeletonData* rig)
    {
        if (component.states.empty() || component.states.size() > 32 || component.transitions.size() > 128)
            throw std::runtime_error("Invalid Animator state count");
        BlendTree::Validate(component.blendTrees, rig);
        ValidateParameters(component.parameters);
        AnimationEvents::Validate(component.events, rig);
        if (component.rootBone.size() > 256 || component.rootBone.find('\0') != std::string::npos ||
            (component.rootMotion && component.rootBone.empty()))
            throw std::runtime_error("Invalid root motion bone name");
        if (rig && component.rootMotion)
            static_cast<void>(FindAnimatorBone(*rig, component.rootBone));
        if (component.ik.size() > 16)
            throw std::runtime_error("Too many IK constraints");
        std::set<std::string> ikNames;
        for (const auto& item : component.ik)
            ValidateIk(item, ikNames, rig);
        std::set<std::string> names;
        for (const auto& state : component.states)
            ValidateState(state, names, component, rig);
        if (!names.contains(component.initialState))
            throw std::runtime_error("Animator initial state missing");
        for (const auto& transition : component.transitions)
            ValidateTransition(transition, names);
    }
}
