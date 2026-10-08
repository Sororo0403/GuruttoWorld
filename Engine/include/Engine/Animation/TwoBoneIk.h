#pragma once
#include <Engine/Animation/Skeleton.h>

namespace Engine
{
    struct TwoBoneIkConstraint
    {
        size_t root=0,middle=1,tip=2;
        std::array<float,3> target{},hint{0,0,1};
        float weight=1;
    };
    class TwoBoneIk final
    {
    public:
        /// <summary>Skeletonのグローバル座標で二関節を解きます。位置・Scaleと先端回転を保持します。</summary>
        /// <remarks>対象チェーンの祖先は正の一様Scaleが必要です。到達不能な目標は可動範囲へ制限します。
        /// 入力検証や計算に失敗した場合、呼び出し元の姿勢を変更しません。</remarks>
        static std::vector<BonePose> Solve(const SkeletonData& rig,const std::vector<BonePose>& pose,
            const TwoBoneIkConstraint& constraint);
    };
}
