#pragma once
#include <Engine/Animation/Skeleton.h>

namespace Engine
{
    class RootMotion final
    {
    public:
        /// <summary>指定ボーンのローカル位置・回転を、ループの累積変換を含めて評価します。</summary>
        /// <remarks>phaseはクリップ長で正規化した時刻です。Scaleは抽出しません。
        /// ループ終端の変換を次の周回へ引き継ぎ、回転した進行方向へ移動します。</remarks>
        static DirectX::XMFLOAT4X4 Sample(const SkeletonData& rig,size_t bone,const std::string& clip,double phase,bool loop);
        /// <summary>fromの姿勢からtoの姿勢への剛体変換を返します。行ベクトル規約でDelta × From = Toです。</summary>
        static DirectX::XMFLOAT4X4 Delta(const SkeletonData& rig,size_t bone,const std::string& clip,double from,double to,bool loop);
    };
}
