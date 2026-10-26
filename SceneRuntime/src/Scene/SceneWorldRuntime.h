#pragma once
#include <SceneRuntime/Scene/SceneWorld.h>
#include <SceneRuntime/Scene/RuntimeSimulation.h>
#include <SceneRuntime/Rendering/RenderExtractor.h>

namespace SceneRuntime
{
    struct SceneWorld::RuntimeData
    {
        std::shared_ptr<const RuntimeScriptDefinitions> definitions;
        RuntimeSession session;
        std::shared_ptr<const RenderPacket> packet;
        RuntimePhysics* physics = nullptr;
        RuntimeAnimation* animation = nullptr;
        RuntimeScriptEffects effects, drained;
        std::map<std::string, PhysicalPose> previousPhysical, currentPhysical;
        double fixedSeconds = 0;
        RuntimeInput input;
        bool pauseRequest = false, reset = false, skipHostPreparation = false;
        std::filesystem::path root;
        SceneWorld* reloading = nullptr;
        /// <summary>CPU実行セッションと表示用の指示領域を作成します。</summary>
        RuntimeData(std::shared_ptr<const WorldDefinition> definition,
                    std::shared_ptr<const RuntimeScriptDefinitions> scripts);
    };
}
