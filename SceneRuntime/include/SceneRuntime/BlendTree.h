#pragma once
#include <Engine/Animation/Skeleton.h>

namespace SceneRuntime
{
    enum class AnimatorBlendType { OneDimensional, Cartesian2D, Direct };
    struct AnimatorBlendMotion
    {
        std::string clip,blendTree,parameter="weight";
        float threshold=0;
        std::array<float,2> position{};
        float speed=1;
        bool operator==(const AnimatorBlendMotion&) const = default;
    };
    struct AnimatorBlendTree
    {
        std::string name="Blend",parameterX="speed",parameterY="MoveForward";
        AnimatorBlendType type=AnimatorBlendType::OneDimensional;
        std::vector<AnimatorBlendMotion> children;
        bool operator==(const AnimatorBlendTree&) const = default;
    };
    struct AnimatorMotionSample
    {
        std::string clip;
        double speed=1;
        float weight=0;
        bool operator==(const AnimatorMotionSample&) const = default;
    };
    class BlendTree final
    {
    public:
        /// <summary>名前・クリップ・重複座標・循環参照・展開数を検証します。</summary>
        static void Validate(const std::vector<AnimatorBlendTree>& trees,const Engine::SkeletonData* rig=nullptr);
        /// <summary>入力から入れ子を展開し、合計が1のクリップ重みを取得します。Directの全重みが0なら初期姿勢です。</summary>
        static std::vector<AnimatorMotionSample> Samples(const std::vector<AnimatorBlendTree>& trees,const std::string& name,const std::map<std::string,float>& parameters);
        /// <summary>クリップ長と子の速度を重み付けした共通サイクルの秒数を取得します。</summary>
        static double Duration(const std::vector<AnimatorMotionSample>& samples,const Engine::SkeletonData& rig);
        /// <summary>全クリップを同じ正規化位相で評価し、位置・拡縮・回転を混ぜます。</summary>
        static std::vector<Engine::BonePose> SamplePose(const std::vector<AnimatorMotionSample>& samples,const Engine::SkeletonData& rig,double phase,bool loop);
    };
}
