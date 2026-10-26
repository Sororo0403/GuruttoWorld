#pragma once
#include <SceneRuntime/Scene/RuntimePipeline.h>
#include <SceneRuntime/Physics/RuntimePhysics.h>
#include <SceneRuntime/Animation/RuntimeAnimation.h>

namespace SceneRuntime
{
    struct RuntimeSimulation
    {
        std::vector<std::unique_ptr<RuntimeSystem>> systems;
        RuntimePhysics* physics = nullptr;
        RuntimeAnimation* animation = nullptr;
        std::function<void(SceneLayout&)> definitionPreparation;
        /// <summary>定義解決と実行順を、同じセッションへ設定します。</summary>
        void Attach(RuntimeSession& session);
        /// <summary>所有するシステムを実行順に渡します。参照先は受取側のパイプラインと同じ寿命です。</summary>
        std::unique_ptr<RuntimePipeline> Pipeline();
        /// <summary>共通の移動・骨姿勢・物理更新を、AppとEditorで同じ順序に構成します。</summary>
        static RuntimeSimulation Create(const std::filesystem::path& root,
                                        std::shared_ptr<const Engine::ContentContext> content = {});
    };
}
