#pragma once
#include <SceneRuntime/Scene/WorldState.h>

namespace SceneRuntime
{
    class RuntimeRagdoll final
    {
    public:
        /// <summary>実行ハンドルを用い、骨と生存するCollider・剛体の対応を検証します。</summary>
        static void Validate(WorldState::ReadView view, EntityHandle owner, const Engine::SkeletonData& rig);
        /// <summary>保存型を変更せず、骨に従う剛体の候補と、物理制御の有効状態を設定します。</summary>
        static void Synchronize(WorldState::Transaction& transaction, EntityHandle owner,
                                const Engine::SkeletonData& rig, const std::vector<Engine::BonePose>& current);
        /// <summary>物理側の確定前姿勢から骨姿勢を合成し、保存データには書き込みません。</summary>
        static std::vector<Engine::BonePose> Pose(WorldState::ReadView view, EntityHandle owner,
                                                  const Engine::SkeletonData& rig,
                                                  const std::vector<Engine::BonePose>& current);
    };
}
