#pragma once
#include <SceneRuntime/Scene/RuntimeSession.h>

namespace SceneRuntime
{
    struct RuntimeAnimationServices
    {
        std::function<std::shared_ptr<const Engine::SkeletonData>(const std::filesystem::path&, std::string&)> load;
        std::function<float(WorldState::ReadView, EntityHandle)> grounded;
        std::function<bool(WorldState::Transaction&, const std::vector<RuntimePose>&, std::string&)> constrain;
    };
    struct RuntimeSkinPose
    {
        EntityHandle entity;
        ComponentInstanceHandle component;
        std::shared_ptr<const Engine::SkeletonData> rig;
        std::shared_ptr<const AnimatorState> state;
        std::uint64_t version = 0;
    };
    class RuntimeAnimation final : public RuntimeSystem
    {
    public:
        /// <summary>GPUを所有せず、骨格の取得と物理の問い合わせ先を受け取ります。</summary>
        explicit RuntimeAnimation(std::filesystem::path root, RuntimeAnimationServices services = {});
        /// <summary>このセッションの骨姿勢と時計を解放します。</summary>
        ~RuntimeAnimation();
        /// <summary>確定済みの骨姿勢を保持します。次の確定後も同じ世代を参照できます。</summary>
        std::vector<RuntimeSkinPose> Snapshot() const;
        /// <summary>指定した確定済み世代の実行状態を借用します。次の公開まで有効です。</summary>
        const AnimatorState* Status(ComponentInstanceHandle instance) const noexcept;
        /// <summary>描画の準備が、現在の取引の骨姿勢を不変な世代として保持します。</summary>
        std::vector<RuntimeSkinPose> CandidateSnapshot() const;
        /// <summary>骨姿勢・RootMotion・IK・Ragdollの未公開候補を作ります。</summary>
        std::unique_ptr<Frame> Begin(WorldState::Transaction& transaction) override;
        /// <summary>Animation自身は物理の光線問い合わせを提供しません。</summary>
        RuntimeScriptQueries Queries() const override;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
