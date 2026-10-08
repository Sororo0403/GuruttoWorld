#pragma once
#include <SceneRuntime/BlendTree.h>
#include <cstdint>

namespace SceneRuntime
{
    struct AnimatorEventKey
    {
        std::string clip,name="event";
        float time=0,value=0,minimumWeight=0;
        std::string stringValue;
        int intValue=0;
        bool operator==(const AnimatorEventKey&) const = default;
    };
    struct AnimatorEventOccurrence
    {
        std::string clip,state,name;
        float time=0,value=0,weight=0;
        std::string stringValue;
        int intValue=0;
        std::uint64_t cycle=0;
        double phase=0;
        size_t keyIndex=0;
        bool operator==(const AnimatorEventOccurrence&) const = default;
    };
    class AnimationEvents final
    {
    public:
        static constexpr size_t Limit=4096;
        /// <summary>クリップ・時刻・イベント名・引数・重み閾値を検証します。</summary>
        static void Validate(const std::vector<AnimatorEventKey>& keys,const Engine::SkeletonData* rig=nullptr);
        /// <summary>Scriptへ渡すイベントのメタデータを検証します。</summary>
        static void ValidateOccurrence(const AnimatorEventOccurrence& event);
        /// <summary>進んだ区間のイベントを時刻順に取得します。同じクリップの重みは合算し、各キーを1ループに1度通知します。</summary>
        static std::vector<AnimatorEventOccurrence> Collect(const std::vector<AnimatorEventKey>& keys,const Engine::SkeletonData& rig,
            const std::string& state,const std::vector<AnimatorMotionSample>& motions,double from,double to,bool loop,bool atStart,
            float fromBlendWeight=1,float toBlendWeight=1);
    };
}
