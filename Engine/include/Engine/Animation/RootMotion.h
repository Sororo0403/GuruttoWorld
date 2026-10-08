#pragma once
#include <Engine/Animation/Skeleton.h>

namespace Engine
{
    class RootMotion final
    {
    public:
        /// <summary>評価済みの姿勢から位置・回転の剛体フレームを取得します。globalなら祖先を含みます。</summary>
        static DirectX::XMFLOAT4X4 PoseFrame(const SkeletonData& rig,const std::vector<BonePose>& pose,size_t bone,bool global=true);
        /// <summary>指定ボーンのローカル位置・回転を、ループの累積変換を含めて評価します。</summary>
        /// <remarks>phaseはクリップ長で正規化した時刻です。Scaleは抽出しません。
        /// ループ終端の変換を次の周回へ引き継ぎ、回転した進行方向へ移動します。</remarks>
        static DirectX::XMFLOAT4X4 Sample(const SkeletonData& rig,size_t bone,const std::string& clip,double phase,bool loop,bool global=false);
        /// <summary>fromの姿勢からtoの姿勢への剛体変換を返します。行ベクトル規約でDelta × From = Toです。</summary>
        static DirectX::XMFLOAT4X4 Delta(const SkeletonData& rig,size_t bone,const std::string& clip,double from,double to,bool loop,bool global=false);
    };
}
