#include <SceneRuntime/Scene/ComponentFieldSchema.h>

namespace SceneRuntime
{
    const ComponentScalarField<PlayerControllerComponent>& PlayerControllerField(
        float PlayerControllerComponent::* const member)
    {
        const auto found = std::find_if(PlayerControllerScalarFields.begin(), PlayerControllerScalarFields.end(),
                                        [member](const auto& field)
                                        {
                                            return field.member == member;
                                        });
        if (found != PlayerControllerScalarFields.end())
            return *found;
        throw std::runtime_error("PlayerController scalar field is not declared");
    }
}
