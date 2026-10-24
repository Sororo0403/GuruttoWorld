#include <Engine/Core/Validation.h>
#include <SceneRuntime/Animation/AnimationEvents.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>

namespace
{
    double Duration(const Engine::SkeletonData& rig, const std::string& name)
    {
        const auto found = std::ranges::find(rig.clips, name, &Engine::SkeletalClip::name);
        if (found == rig.clips.end() || !std::isfinite(found->duration) || found->duration <= 0)
            throw std::runtime_error("Animation event clip missing or invalid: " + name);
        return found->duration;
    }
    bool Text(const std::string& value, size_t maximum, bool empty = false)
    {
        return (empty || !value.empty()) && value.size() <= maximum && value.find('\0') == std::string::npos;
    }
    struct EventCycles
    {
        double clipWeight, position, first, last, blendDelta;
    };
    void ValidateInterval(const SceneRuntime::AnimationEventInterval& interval)
    {
        const auto& [from, to, loop, atStart, fromBlendWeight, toBlendWeight] = interval;
        if (!std::isfinite(from) || !std::isfinite(to) || from < 0 || to < from ||
            !Engine::InRange(fromBlendWeight, 0, 1) || !Engine::InRange(toBlendWeight, 0, 1))
            throw std::runtime_error("Invalid animation event interval");
    }

    std::map<std::string, double> MotionWeights(const std::vector<SceneRuntime::AnimatorMotionSample>& motions)
    {
        std::map<std::string, double> weights;
        for (const auto& motion : motions)
        {
            if (!Engine::InRange(motion.weight, 0, 1))
                throw std::runtime_error("Invalid event motion weight");
            if (!motion.clip.empty() && motion.weight > 0)
                weights[motion.clip] += motion.weight;
        }
        return weights;
    }

    void AdjustBlendCycles(double& first, double& last, const SceneRuntime::AnimationEventInterval& interval,
                           double position, double clipWeight, double minimum, double blendDelta)
    {
        const auto& [from, to, loop, atStart, fromBlendWeight, toBlendWeight] = interval;
        if (blendDelta != 0)
        {
            const double progress = (minimum / clipWeight - fromBlendWeight) / blendDelta;
            const double crossing = from + progress * (to - from) - position;
            if (blendDelta > 0 && progress > 0)
                first = std::max(first, std::ceil(crossing));
            if (blendDelta < 0 && progress < 1)
                last = std::min(last, std::floor(crossing));
        }
    }
    double MinimumWeight(const SceneRuntime::AnimatorEventKey& key)
    {
        return key.minimumWeight == 0 ? 0
                                      : (static_cast<double>(key.minimumWeight) +
                                         std::nextafter(key.minimumWeight, -(std::numeric_limits<float>::infinity)())) *
                                            .5;
    }
    std::optional<EventCycles> EventWindow(const SceneRuntime::AnimatorEventKey& key, const Engine::SkeletonData& rig,
                                           const std::map<std::string, double>& weights,
                                           const SceneRuntime::AnimationEventInterval& interval)
    {
        const auto& [from, to, loop, atStart, fromBlendWeight, toBlendWeight] = interval;
        const auto active = weights.find(key.clip);
        if (active == weights.end())
            return std::nullopt;
        const double clipWeight = std::min(active->second, 1.0);
        const double minimum = MinimumWeight(key);
        if (clipWeight * std::max(fromBlendWeight, toBlendWeight) < minimum)
            return std::nullopt;
        const double duration = Duration(rig, key.clip), position = key.time / duration;
        // floatのキー時刻と累積時計の丸め差を吸収します。両端へ同じ許容値を加え、次の区間で再通知しません。
        const double epsilon = std::min(1e-7, 1e-6 / duration);
        const bool start = atStart && from == 0;
        double first = loop ? std::max(0.0, start ? 0.0 : std::floor(from + epsilon - position) + 1) : 0;
        double last = loop ? std::floor(to + epsilon - position) : 0;
        if (last < first || (!loop && ((!start && position <= from + epsilon) || position > to + epsilon)))
            return std::nullopt;
        const double blendDelta = static_cast<double>(toBlendWeight) - fromBlendWeight;
        AdjustBlendCycles(first, last, interval, position, clipWeight, minimum, blendDelta);
        if (last < first)
            return std::nullopt;
        if (last >= 9007199254740992.0 ||
            last - first + 1 > static_cast<double>(SceneRuntime::AnimationEvents::Limit + 2))
            throw std::runtime_error("Animation event cycle count exceeds limit");
        return EventCycles{clipWeight, position, first, last, blendDelta};
    }

    void EmitEvents(const SceneRuntime::AnimatorEventKey& key, size_t index, const std::string& state,
                    const SceneRuntime::AnimationEventInterval& interval, const EventCycles& window,
                    std::vector<SceneRuntime::AnimatorEventOccurrence>& events)
    {
        const auto& [from, to, loop, atStart, fromBlendWeight, toBlendWeight] = interval;
        const auto& [clipWeight, position, first, last, blendDelta] = window;
        for (double cycle = first; cycle <= last; ++cycle)
        {
            const double phase = cycle + position;
            const double progress = std::clamp((phase - from) / (to - from), 0.0, 1.0);
            const float weight = static_cast<float>(clipWeight * (fromBlendWeight + blendDelta * progress));
            if (weight < key.minimumWeight)
                continue;
            if (events.size() >= SceneRuntime::AnimationEvents::Limit)
                throw std::runtime_error("Animation event count exceeds limit");
            events.push_back({key.clip, state, key.name, key.time, key.value, weight, key.stringValue, key.intValue,
                              static_cast<std::uint64_t>(cycle), phase, index});
        }
    }

}
namespace SceneRuntime
{
    void AnimationEvents::Validate(const std::vector<AnimatorEventKey>& keys, const Engine::SkeletonData* rig)
    {
        if (keys.size() > 256)
            throw std::runtime_error("Too many animation event keys");
        for (const auto& key : keys)
        {
            if (!Text(key.clip, 256) || !Text(key.name, 128) || !Text(key.stringValue, 1024, true) ||
                !Engine::InRange(key.time, 0, 1000000) || !Engine::InRange(key.value, -1000000, 1000000) ||
                !Engine::InRange(key.minimumWeight, 0, 1))
                throw std::runtime_error("Invalid animation event key");
            if (rig && key.time > Duration(*rig, key.clip))
                throw std::runtime_error("Animation event is past clip end");
        }
    }
    std::vector<AnimatorEventOccurrence> AnimationEvents::Collect(const std::vector<AnimatorEventKey>& keys,
                                                                  const Engine::SkeletonData& rig,
                                                                  const std::string& state,
                                                                  const std::vector<AnimatorMotionSample>& motions,
                                                                  const AnimationEventInterval& interval)
    {
        const auto& [from, to, loop, atStart, fromBlendWeight, toBlendWeight] = interval;

        Validate(keys, &rig);
        ValidateInterval(interval);
        std::vector<AnimatorEventOccurrence> events;
        if (to == from || keys.empty())
            return events;
        const auto weights = MotionWeights(motions);
        for (size_t index = 0; index < keys.size(); ++index)
        {
            const auto& key = keys[index];
            const auto window = EventWindow(key, rig, weights, interval);
            if (!window)
                continue;
            EmitEvents(key, index, state, interval, *window, events);
        }
        std::ranges::stable_sort(events,
                                 [](const auto& a, const auto& b)
                                 {
                                     if (a.phase != b.phase)
                                         return a.phase < b.phase;
                                     if ((a.time == 0) != (b.time == 0))
                                         return a.time != 0;
                                     return a.keyIndex < b.keyIndex;
                                 });
        return events;
    }
    void AnimationEvents::ValidateOccurrence(const AnimatorEventOccurrence& event)
    {
        if (!Text(event.clip, 256) || !Text(event.state, 128) || !Text(event.name, 128) ||
            !Text(event.stringValue, 1024, true) || !Engine::InRange(event.time, 0, 1000000) ||
            !Engine::InRange(event.value, -1000000, 1000000) || !Engine::InRange(event.weight, 0, 1) ||
            !std::isfinite(event.phase) || event.phase < 0 || event.cycle >= 9007199254740992ULL ||
            event.keyIndex >= 256 || event.phase < static_cast<double>(event.cycle) ||
            event.phase > static_cast<double>(event.cycle) + 1)
            throw std::runtime_error("Invalid animation event metadata");
    }
}
