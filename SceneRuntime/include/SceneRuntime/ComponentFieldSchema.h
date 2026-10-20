#pragma once
#include <SceneRuntime/SceneComponents.h>
#include <Engine/Core/Validation.h>
#include <string_view>

namespace SceneRuntime
{
    template <class Component> struct ComponentScalarField
    {
        std::string_view name;
        float Component::* member;
        float minimum, maximum;
        /// <summary>読込・保存・Inspectorが共有する保存範囲を確認します。</summary>
        bool Accepts(double value) const
        {
            return Engine::InRange(value, minimum, maximum);
        }
    };
    inline constexpr std::array PlayerControllerScalarFields{
        ComponentScalarField<PlayerControllerComponent>{"moveSpeed", &PlayerControllerComponent::moveSpeed, 0, 1000},
        ComponentScalarField<PlayerControllerComponent>{"gravity", &PlayerControllerComponent::gravity, 0, 1000},
        ComponentScalarField<PlayerControllerComponent>{"jumpSpeed", &PlayerControllerComponent::jumpSpeed, 0, 1000},
        ComponentScalarField<PlayerControllerComponent>{"maxSlopeDegrees", &PlayerControllerComponent::maxSlopeDegrees,
                                                        0, 89},
        ComponentScalarField<PlayerControllerComponent>{"stepHeight", &PlayerControllerComponent::stepHeight, 0, 10}};
    /// <summary>PlayerControllerのフィールドに対応する共通の制約を取得します。</summary>
    inline const ComponentScalarField<PlayerControllerComponent>& PlayerControllerField(
        float PlayerControllerComponent::* member)
    {
        for (const auto& field : PlayerControllerScalarFields)
        {
            if (field.member == member)
            {
                return field;
            }
        }
        throw std::runtime_error("PlayerController scalar field is not declared");
    }
}
