#pragma once
#include <SceneRuntime/Scene/RuntimeSession.h>

namespace SceneRuntime
{
    class RuntimeNavigation final : public RuntimeSystem
    {
    public:
        /// <summary>型別のNavMesh値とワールド行列から、ワールド空間の経路を取得します。</summary>
        static std::vector<std::array<float, 3>> Path(WorldState::ReadView view, EntityHandle mesh,
                                                      const std::array<float, 3>& start,
                                                      const std::array<float, 3>& destination);
        /// <summary>同じ更新前の階層で移動を計画し、エージェントと子孫を一度だけ移動します。</summary>
        static bool Advance(WorldState::Transaction& transaction, double seconds, std::string& error);
        /// <summary>固定更新の移動を、外側のWorld取引へ接続します。</summary>
        std::unique_ptr<Frame> Begin(WorldState::Transaction& transaction) override;
        /// <summary>ナビゲーション自身は物理の光線問い合わせを提供しません。</summary>
        RuntimeScriptQueries Queries() const override;
    };
}
