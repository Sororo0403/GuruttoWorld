#include <SceneRuntime/Animation.h>
#include <algorithm>
#include <cmath>

namespace SceneRuntime
{
    bool Animation::Valid(const AnimationTrack& track)
    {
        const bool property = track.property == "position" || track.property == "rotation" ||
            track.property == "uiPosition" || track.property == "uiSize" ||
            track.property == "uiRotation" || track.property == "opacity";
        const bool easing = track.easing == "linear" || track.easing == "smooth" ||
            track.easing == "outCubic" || track.easing == "outBack";
        if (!property || !easing || track.clock.empty() || track.clock.size() > 128 ||
            track.clock.find_first_of("=&\0", 0, 3) != std::string::npos ||
            !std::isfinite(track.delay) || track.delay < 0 || track.delay > 600 ||
            track.keys.empty() || track.keys.size() > 128) return false;
        float previous = -1;
        for (const auto& key : track.keys)
        {
            if (!std::isfinite(key.time) || key.time <= previous || key.time < 0 || key.time > 600) return false;
            for (const float value : key.value)
                if (!std::isfinite(value) || std::abs(value) > 100000) return false;
            if (track.property == "uiSize" && (key.value[0] < 0 || key.value[1] < 0)) return false;
            if (track.property == "opacity" && (key.value[0] < 0 || key.value[0] > 1)) return false;
            previous = key.time;
        }
        return !track.loop || track.keys.back().time > 0;
    }
    std::optional<std::array<float, 4>> Animation::Sample(const AnimationTrack& track,
        const std::map<std::string, float>& clocks)
    {
        const auto clock = clocks.find(track.clock);
        if (clock == clocks.end() || !std::isfinite(clock->second) || clock->second < 0 || track.keys.empty()) return {};
        float time = std::max(0.0f, clock->second - track.delay);
        if (track.loop && track.keys.back().time > 0) time = std::fmod(time, track.keys.back().time);
        if (time <= track.keys.front().time) return track.keys.front().value;
        const auto next = std::upper_bound(track.keys.begin(), track.keys.end(), time,
            [](float value, const AnimationKey& key) { return value < key.time; });
        if (next == track.keys.end()) return track.keys.back().value;
        const auto& previous = *(next - 1);
        float progress = (time - previous.time) / (next->time - previous.time);
        if (track.easing == "smooth") progress = progress * progress * (3 - 2 * progress);
        else if (track.easing == "outCubic") progress = 1 - std::pow(1 - progress, 3.0f);
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
