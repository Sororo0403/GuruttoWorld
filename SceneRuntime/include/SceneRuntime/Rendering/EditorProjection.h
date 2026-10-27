#pragma once
#include <SceneRuntime/Rendering/RenderPacket.h>
#include <SceneRuntime/Rendering/SceneResourcePreparation.h>

namespace SceneRuntime
{
    /// <summary>実行セッションを持たず、編集表示の候補と確定した描画世代を管理します。</summary>
    class EditorProjection final
    {
    public:
        /// <summary>編集データと準備済み資源から候補を抽出し、現在の表示世代は保持します。</summary>
        std::shared_ptr<const RenderPacket> Prepare(const PreparedSceneResources& prepared, ID3D12Device* device,
                                                    std::uint64_t generation, std::string& error) const;
        /// <summary>すべての準備が成功した描画世代だけを公開します。</summary>
        void Commit(std::shared_ptr<const RenderPacket> packet) noexcept
        {
            packet_.swap(packet);
        }
        /// <summary>Worldや文書の可変参照を持たない、現在の表示世代を取得します。</summary>
        const std::shared_ptr<const RenderPacket>& Snapshot() const noexcept
        {
            return packet_;
        }

    private:
        std::shared_ptr<const RenderPacket> packet_;
    };
}
