#include <Engine/Core/Validation.h>
#include <SceneRuntime/Animation/BlendTree.h>
#include "BlendTreeValidation.h"
using namespace SceneRuntime::Detail;
#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>
#include <set>
#include <stdexcept>

namespace
{
    bool Identifier(const std::string& value, size_t limit = 128)
    {
        return !value.empty() && value.size() <= limit && value.find('\0') == std::string::npos;
    }

    double Parameter(const std::map<std::string, float>& parameters, const std::string& name)
    {
        const auto found = parameters.find(name);
        const double value = found == parameters.end() ? 0 : found->second;
        if (!std::isfinite(value))
            throw std::runtime_error("Nonfinite Blend Tree parameter: " + name);
        return value;
    }
    const SceneRuntime::AnimatorBlendTree& Tree(const std::vector<SceneRuntime::AnimatorBlendTree>& trees,
                                                const std::string& name)
    {
        const auto found = std::ranges::find(trees, name, &SceneRuntime::AnimatorBlendTree::name);
        if (found == trees.end())
            throw std::runtime_error("Blend Tree missing: " + name);
        return *found;
    }
    std::vector<double> Weights(const SceneRuntime::AnimatorBlendTree& tree,
                                const std::map<std::string, float>& parameters)
    {
        using SceneRuntime::AnimatorBlendType;
        std::vector<double> weights(tree.children.size());
        if (tree.type == AnimatorBlendType::Direct)
        {
            for (size_t index = 0; index < weights.size(); ++index)
                weights[index] = std::max(0.0, Parameter(parameters, tree.children[index].parameter));
        }
        else if (tree.type == AnimatorBlendType::OneDimensional)
        {
            const auto value = Parameter(parameters, tree.parameterX);
            std::vector<size_t> order(weights.size());
            std::iota(order.begin(), order.end(), 0);
            std::ranges::sort(order,
                              [&](size_t a, size_t b)
                              {
                                  return tree.children[a].threshold < tree.children[b].threshold;
                              });
            if (value <= tree.children[order.front()].threshold)
                weights[order.front()] = 1;
            else if (value >= tree.children[order.back()].threshold)
                weights[order.back()] = 1;
            else
                for (size_t index = 1; index < order.size(); ++index)
                {
                    const double next = tree.children[order[index]].threshold,
                                 previous = tree.children[order[index - 1]].threshold;
                    if (value > next)
                        continue;
                    weights[order[index]] = (value - previous) / (next - previous);
                    weights[order[index - 1]] = 1 - weights[order[index]];
                    break;
                }
        }
        else
        {
            const double x = Parameter(parameters, tree.parameterX), y = Parameter(parameters, tree.parameterY);
            // 座標間に線形な重みの帯を作り、各子の最も小さい帯を正規化します。
            for (size_t index = 0; index < weights.size(); ++index)
            {
                const auto& point = tree.children[index].position;
                double weight = 1;
                for (size_t other = 0; other < weights.size(); ++other)
                {
                    if (index == other)
                        continue;
                    const auto& target = tree.children[other].position;
                    const double dx = static_cast<double>(target[0]) - point[0],
                                 dy = static_cast<double>(target[1]) - point[1];
                    weight = std::min(
                        weight,
                        std::clamp(1 - ((x - point[0]) * dx + (y - point[1]) * dy) / (dx * dx + dy * dy), 0.0, 1.0));
                }
                weights[index] = weight;
            }
        }
        const double total = std::accumulate(weights.begin(), weights.end(), 0.0);
        if (total > 0)
            std::ranges::transform(weights, weights.begin(),
                                   [&](double weight)
                                   {
                                       return weight / total;
                                   });
        return weights;
    }
    void Expand(const std::vector<SceneRuntime::AnimatorBlendTree>& trees, const std::string& name,
                const std::map<std::string, float>& parameters, double weight, double speed,
                std::vector<SceneRuntime::AnimatorMotionSample>& samples)
    {
        const auto& tree = Tree(trees, name);
        const auto weights = Weights(tree, parameters);
        if (std::none_of(weights.begin(), weights.end(),
                         [](double value)
                         {
                             return value > 0;
                         }))
        {
            samples.push_back({"", speed, static_cast<float>(weight)});
            return;
        }
        for (size_t index = 0; index < weights.size(); ++index)
        {
            if (weights[index] <= 0)
                continue;
            const auto& child = tree.children[index];
            if (!child.blendTree.empty())
                Expand(trees, child.blendTree, parameters, weight * weights[index], speed * child.speed, samples);
            else
                samples.push_back({child.clip, speed * child.speed, static_cast<float>(weight * weights[index])});
        }
    }

    void ValidateMotion(const SceneRuntime::AnimatorBlendMotion& child)
    {
        if (!Engine::ValidString(child.clip, 256) || !Engine::ValidString(child.blendTree, 128) ||
            (!child.clip.empty() && !child.blendTree.empty()) || !Engine::InRange(child.speed, .001f, 1000) ||
            !Engine::InRange(child.threshold, -1000000, 1000000) ||
            std::ranges::any_of(child.position,
                                [](float value)
                                {
                                    return !Engine::InRange(value, -1000000, 1000000);
                                }))
            throw std::runtime_error("Invalid Blend Tree motion");
    }
    void ValidateMotions(const SceneRuntime::AnimatorBlendTree& tree, const Engine::SkeletonData* rig)
    {
        using namespace SceneRuntime;
        std::set<float> thresholds;
        std::set<std::array<float, 2>> points;
        for (const auto& child : tree.children)
        {
            ValidateMotion(child);
            if (tree.type == AnimatorBlendType::OneDimensional && !thresholds.insert(child.threshold).second)
                throw std::runtime_error("Duplicate Blend Tree threshold");
            if (tree.type == AnimatorBlendType::Cartesian2D && !points.insert(child.position).second)
                throw std::runtime_error("Duplicate Blend Tree position");
            if (tree.type == AnimatorBlendType::Direct && !Identifier(child.parameter))
                throw std::runtime_error("Direct Blend Tree parameter missing");
            if (rig && child.blendTree.empty())
                ClipDuration(*rig, child.clip);
        }
    }

    struct BlendValidationGraph
    {
        explicit BlendValidationGraph(const std::vector<SceneRuntime::AnimatorBlendTree>& source) : trees(source) {}
        const std::vector<SceneRuntime::AnimatorBlendTree>& trees;
        std::map<std::string, std::pair<size_t, size_t>> dimensions;
        std::set<std::string> visiting;
        std::pair<size_t, size_t> Visit(const std::string& name)
        {
            if (const auto found = dimensions.find(name); found != dimensions.end())
                return found->second;
            if (!visiting.insert(name).second)
                throw std::runtime_error("Cyclic Blend Tree reference");
            size_t depth = 1, leaves = 0;
            for (const auto& child : Tree(trees, name).children)
            {
                if (child.blendTree.empty())
                    ++leaves;
                else
                {
                    const auto nested = Visit(child.blendTree);
                    depth = std::max(depth, nested.first + 1);
                    leaves += nested.second;
                }
                if (depth > 8 || leaves > 256)
                    throw std::runtime_error("Blend Tree expansion exceeds limit");
            }
            visiting.erase(name);
            const auto result = std::pair{depth, leaves};
            dimensions.emplace(name, result);
            return result;
        }
    };

}
namespace SceneRuntime
{
    void BlendTree::Validate(const std::vector<AnimatorBlendTree>& trees, const Engine::SkeletonData* rig)
    {
        if (trees.size() > 32)
            throw std::runtime_error("Too many Blend Trees");
        std::set<std::string> names;
        for (const auto& tree : trees)
        {
            if (!Identifier(tree.name) || !names.insert(tree.name).second || tree.children.empty() ||
                tree.children.size() > 32 ||
                (tree.type != AnimatorBlendType::OneDimensional && tree.type != AnimatorBlendType::Cartesian2D &&
                 tree.type != AnimatorBlendType::Direct))
                throw std::runtime_error("Invalid Blend Tree");
            if (tree.type != AnimatorBlendType::Direct && !Identifier(tree.parameterX))
                throw std::runtime_error("Blend Tree parameter missing");
            if (tree.type == AnimatorBlendType::Cartesian2D && !Identifier(tree.parameterY))
                throw std::runtime_error("2D Blend Tree parameter missing");
            ValidateMotions(tree, rig);
        }
        BlendValidationGraph graph{trees};
        for (const auto& tree : trees)
            graph.Visit(tree.name);
    }
    std::vector<AnimatorMotionSample> BlendTree::Samples(const std::vector<AnimatorBlendTree>& trees,
                                                         const std::string& name,
                                                         const std::map<std::string, float>& parameters)
    {
        Validate(trees);
        std::vector<AnimatorMotionSample> samples;
        Expand(trees, name, parameters, 1, 1, samples);
        const double total = std::accumulate(samples.begin(), samples.end(), 0.0,
                                             [](double value, const auto& sample)
                                             {
                                                 return value + sample.weight;
                                             });
        if (total <= 0)
            return {{"", 1, 1}};
        for (auto& sample : samples)
            sample.weight = static_cast<float>(sample.weight / total);
        return samples;
    }
    double BlendTree::Duration(const std::vector<AnimatorMotionSample>& samples, const Engine::SkeletonData& rig)
    {
        double duration = 0, total = 0;
        for (const auto& sample : samples)
        {
            ValidateSample(sample);
            if (sample.weight == 0)
                continue;
            duration += sample.weight * ClipDuration(rig, sample.clip) / sample.speed;
            total += sample.weight;
        }
        if (total <= 0 || !std::isfinite(duration) || duration <= 0)
            throw std::runtime_error("Invalid Blend Tree duration");
        return duration / total;
    }

}
