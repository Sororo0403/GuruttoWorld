#include <SceneRuntime/Scene/RuntimeSimulation.h>
#include <SceneRuntime/Gameplay/RuntimeNavigation.h>

namespace SceneRuntime
{
    RuntimeSimulation RuntimeSimulation::Create(const std::filesystem::path& root)
    {
        RuntimeSimulation result;
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
    std::unique_ptr<RuntimePipeline> RuntimeSimulation::Pipeline()
    {
        return std::make_unique<RuntimePipeline>(std::move(systems));
    }
}
