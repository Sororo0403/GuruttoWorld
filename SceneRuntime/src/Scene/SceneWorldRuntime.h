#pragma once
#include <SceneRuntime/Scene/SceneWorld.h>
#include <SceneRuntime/Scene/RuntimeSimulation.h>
#include <SceneRuntime/Rendering/RenderExtractor.h>

namespace SceneRuntime
{
    struct SceneWorld::RuntimeData
    {
        RuntimeSession session;
        std::shared_ptr<const RenderPacket> packet;
        RuntimePhysics* physics = nullptr;
        RuntimeAnimation* animation = nullptr;
        RuntimeScriptEffects effects, drained;
        RuntimeInput input;
        bool pauseRequest = false, reset = false, skipHostPreparation = false;
        std::filesystem::path root;
        /// <summary>CPU実行セッションと表示用の指示領域を作成します。</summary>
        explicit RuntimeData(std::shared_ptr<const WorldDefinition> definition);
    };
}
