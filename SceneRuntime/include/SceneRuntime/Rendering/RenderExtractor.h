#pragma once
#include <SceneRuntime/Rendering/RenderPacket.h>
#include <SceneRuntime/Scene/WorldState.h>
#include <SceneRuntime/Physics/RuntimePhysicalHistory.h>

namespace SceneRuntime
{
    class RenderExtractor final
    {
    public:
        /// <summary>候補から不変の表示情報を抽出します。保存配置・可変World・Scriptの参照はpacketへ持ち越しません。</summary>
        static std::shared_ptr<const RenderPacket> Extract(WorldState::ReadView view, RenderResourceResolver& resources,
                                                           std::uint64_t frame, std::uint64_t device,
                                                           const RuntimePhysicalHistory& history, double interpolation,
                                                           std::string& error);
    };
}
