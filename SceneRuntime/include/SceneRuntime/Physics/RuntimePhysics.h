#pragma once
#include <SceneRuntime/Scene/RuntimeSession.h>
#include <SceneRuntime/Physics/RuntimePhysicalHistory.h>

namespace SceneRuntime
{
    class RuntimePhysics final : public RuntimeSystem
    {
    public:
        /// <summary>物理状態と形状の参照元を、実行セッション単位で所有します。</summary>
        explicit RuntimePhysics(std::filesystem::path root);
        /// <summary>物理ワールドを解放します。</summary>
        ~RuntimePhysics();
        /// <summary>物理状態・接触・入力に対して、巻き戻せる候補を作ります。</summary>
        std::unique_ptr<Frame> Begin(WorldState::Transaction& transaction) override;
        /// <summary>この物理世代の読み取り専用の問い合わせを取得します。</summary>
        RuntimeScriptQueries Queries() const override;
        /// <summary>同じ取引のAnimationが予約した移動を、物理の衝突問い合わせで制限します。</summary>
        bool Constrain(WorldState::Transaction& transaction, const std::vector<RuntimePose>& previous,
                       std::string& error) const;
        /// <summary>同じ取引のプレイヤー接地状態を返します。未作成の状態は1です。</summary>
        float Grounded(WorldState::ReadView view, EntityHandle entity) const;
        /// <summary>現在の候補、または確定済みの固定姿勢の前後を借用します。</summary>
        const RuntimePhysicalHistory& History() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
