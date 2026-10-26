#pragma once
#include <SceneRuntime/Scene/RuntimeSession.h>

namespace SceneRuntime
{
    class RuntimePipeline final : public RuntimeSystem
    {
    public:
        /// <summary>実行・物理・Animation・描画の参加先を、実行順に所有します。</summary>
        explicit RuntimePipeline(std::vector<std::unique_ptr<RuntimeSystem>> systems);
        /// <summary>すべての参加先の候補を作り、一つの確定境界にまとめます。</summary>
        std::unique_ptr<Frame> Begin(WorldState::Transaction& transaction) override;
        /// <summary>各問い合わせ先から最も近い物理交点を取得します。</summary>
        RuntimeScriptQueries Queries() const override;

    private:
        std::vector<std::unique_ptr<RuntimeSystem>> systems_;
    };
}
