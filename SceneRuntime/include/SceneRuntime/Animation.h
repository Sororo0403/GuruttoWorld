#pragma once
#include <array>
#include <vector>
#include <string>
#include <optional>
#include <map>

namespace SceneRuntime
{
    struct AnimationKey
    {
        float time = 0;
        std::array<float, 4> value{};
        bool operator==(const AnimationKey&) const = default;
    };
    struct AnimationTrack
    {
        std::string property = "uiPosition";
        std::string clock = "sceneTime";
        std::string easing = "smooth";
        float delay = 0;
        bool loop = false;
        std::vector<AnimationKey> keys{{0, {}}, {1, {}}};
        bool operator==(const AnimationTrack&) const = default;
    };
    struct AnimationComponent
    {
        std::string id = "animation";
        bool enabled = true;
        std::vector<AnimationTrack> tracks;
        bool operator==(const AnimationComponent&) const = default;
    };
    class Animation final
    {
    public:
        /// <summary>シーンの時計からトラックを評価します。負の時計は未開始として扱います。</summary>
        static std::optional<std::array<float, 4>> Sample(const AnimationTrack& track,
            const std::map<std::string, float>& clocks);
        /// <summary>プロパティ・補間・時刻・値の保存可能性を検証します。</summary>
        static bool Valid(const AnimationTrack& track);
    };
}
