#include <SceneRuntime/Scene/WorldDefinition.h>

namespace SceneRuntime
{
    WorldDefinition::WorldDefinition(SceneLayout source) : source_(std::move(source)) {}

    std::shared_ptr<const WorldDefinition> WorldDefinition::Compile(SceneLayout source)
    {
        source.Validate();
        return std::shared_ptr<const WorldDefinition>(new WorldDefinition(std::move(source)));
    }

    const SceneLayout& WorldDefinition::Source() const noexcept
    {
        return source_;
    }
}
