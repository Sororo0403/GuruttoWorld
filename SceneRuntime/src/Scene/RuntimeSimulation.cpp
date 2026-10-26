#include <SceneRuntime/Scene/RuntimeSimulation.h>
#include <SceneRuntime/Gameplay/RuntimeNavigation.h>
#include <Engine/Assets/AssetDatabase.h>

namespace SceneRuntime
{
    RuntimeSimulation RuntimeSimulation::Create(const std::filesystem::path& root,
                                                std::shared_ptr<const Engine::ContentContext> content)
    {
        RuntimeSimulation result;
        if (content)
        {
            content->RequireRoot(root);
            result.definitionPreparation = [content](SceneLayout& layout)
            {
                content->RequireConfiguration();
                layout.ResolveAssets(Engine::AssetDatabase(content, true));
            };
        }
        auto physics = std::make_unique<RuntimePhysics>(root);
        result.physics = physics.get();
        RuntimeAnimationServices services;
        services.grounded = [target = result.physics](WorldState::ReadView view, EntityHandle entity)
        {
            return target->Grounded(view, entity);
        };
        services.constrain = [target = result.physics](WorldState::Transaction& transaction,
                                                       const std::vector<RuntimePose>& previous, std::string& error)
        {
            return target->Constrain(transaction, previous, error);
        };
        auto animation = std::make_unique<RuntimeAnimation>(root, std::move(services));
        result.animation = animation.get();
        result.systems.push_back(std::make_unique<RuntimeNavigation>());
        result.systems.push_back(std::move(animation));
        result.systems.push_back(std::move(physics));
        return result;
    }
    void RuntimeSimulation::Attach(RuntimeSession& session)
    {
        session.SetDefinitionPreparation(std::move(definitionPreparation));
        session.SetSystem(Pipeline());
    }
    std::unique_ptr<RuntimePipeline> RuntimeSimulation::Pipeline()
    {
        return std::make_unique<RuntimePipeline>(std::move(systems));
    }
}
