#include <SceneRuntime/Animation.h>
#include <algorithm>
#include <cmath>
#include <string_view>

namespace
{
    bool ValidConfiguration(const SceneRuntime::AnimationTrack& track)
    {
        using namespace std::literals;
        const auto properties = SceneRuntime::Animation::Properties();
        constexpr std::array easings{"linear"sv, "smooth"sv, "outCubic"sv, "outBack"sv};
        if (std::none_of(properties.begin(), properties.end(),
                         [&](const auto& p)
                         {
                             return track.property == p.name;
                         }) ||
            std::find(easings.begin(), easings.end(), track.easing) == easings.end())
            return false;
        return !track.clock.empty() && track.clock.size() <= 128 &&
               track.clock.find_first_of("=&\0", 0, 3) == std::string::npos && std::isfinite(track.delay) &&
               track.delay >= 0 && track.delay <= 600 && !track.keys.empty() && track.keys.size() <= 128;
    }
    bool ValidValue(const SceneRuntime::AnimationKey& key, const std::string& property)
    {
        if (!std::all_of(key.value.begin(), key.value.end(),
                         [](float value)
                         {
                             return std::isfinite(value) && std::abs(value) <= 1000000;
                         }))
            return false;
        const auto properties = SceneRuntime::Animation::Properties();
        const auto info = std::find_if(properties.begin(), properties.end(),
                                       [&](const auto& p)
                                       {
                                           return property == p.name;
                                       });
        if (info == properties.end())
            return false;
        for (unsigned int i = 0; i < info->channels; ++i)
            if (key.value[i] < info->minimum || key.value[i] > info->maximum ||
                (property == "scale" && std::abs(key.value[i]) < .00001f))
                return false;
        return true;
    }
}

namespace SceneRuntime
{
    bool Animation::Valid(const AnimationTrack& track)
    {
        if (!ValidConfiguration(track))
            return false;
        float previous = -1;
        for (const auto& key : track.keys)
        {
            if (!std::isfinite(key.time) || key.time <= previous || key.time < 0 || key.time > 600)
                return false;
            if (!ValidValue(key, track.property))
                return false;
            previous = key.time;
        }
        return !track.loop || track.keys.back().time > 0;
    }
    std::optional<std::array<float, 4>> Animation::Sample(const AnimationTrack& track,
                                                          const std::map<std::string, float>& clocks)
    {
        const auto clock = clocks.find(track.clock);
        if (clock == clocks.end() || !std::isfinite(clock->second) || clock->second < 0 || track.keys.empty())
            return {};
        float time = std::max(0.0f, clock->second - track.delay);
        if (track.loop && track.keys.back().time > 0)
            time = std::fmod(time, track.keys.back().time);
        if (time <= track.keys.front().time)
            return track.keys.front().value;
        const auto next = std::upper_bound(track.keys.begin(), track.keys.end(), time,
                                           [](float value, const AnimationKey& key)
                                           {
                                               return value < key.time;
                                           });
        if (next == track.keys.end())
            return track.keys.back().value;
        const auto& previous = *(next - 1);
        float progress = (time - previous.time) / (next->time - previous.time);
        if (track.easing == "smooth")
            progress = progress * progress * (3 - 2 * progress);
        else if (track.easing == "outCubic")
            progress = 1 - std::pow(1 - progress, 3.0f);
        else if (track.easing == "outBack")
        {
            const float t = progress - 1;
            progress = 1 + 2.70158f * t * t * t + 1.70158f * t * t;
        }
        std::array<float, 4> result{};
        for (size_t axis = 0; axis < result.size(); ++axis)
            result[axis] = std::lerp(previous.value[axis], next->value[axis], progress);
        return result;
    }
}
