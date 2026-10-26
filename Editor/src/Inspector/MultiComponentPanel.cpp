#include "MultiComponentPanel.h"

namespace Editor
{
    auto MultiComponentPanel::Draw(EditState& state, const SceneRuntime::SceneLayout& layout,
                                   const ProjectCatalog* catalog) -> void
    {
        auto objects = SelectedObjects(layout, state);
        if (objects.size() < 2 || objects.size() != state.SelectedIds().size())
            return;
        bool changed = false;
        const auto group = [&](auto component, const char* title, auto draw)
        {
            changed |= DrawGroup(state, objects, component, title, draw);
        };
        const auto scalar = [&](auto component, auto field, const char* label, float low = 0, float high = 100000)
        {
            changed |= DrawScalar(state, objects, component, field, label, low, high);
        };
        const auto boolean = [&](auto component, auto field, const char* label)
        {
            changed |= Field(state, objects, component, field, label,
                             [&](auto& value)
                             {
                                 return ImGui::Checkbox(label, &value);
                             });
        };
        const auto vector = [&](auto component, auto field, const char* label, float low = -100000, float high = 100000)
        {
            changed |= Field(state, objects, component, field, label,
                             [&](auto& value)
                             {
                                 if constexpr (std::tuple_size_v<std::remove_reference_t<decltype(value)>> == 2)
                                     return ImGui::DragFloat2(label, value.data(), .1f, low, high, "%.3f",
                                                              ImGuiSliderFlags_AlwaysClamp);
                                 else
                                     return ImGui::DragFloat3(label, value.data(), .1f, low, high, "%.3f",
                                                              ImGuiSliderFlags_AlwaysClamp);
                             });
        };
        const auto asset = [&](auto component, auto field, const char* label, AssetKind kind, bool empty)
        {
            changed |= DrawAsset(state, objects, component, field, label, kind, empty, catalog);
        };
        using P = SceneRuntime::ScenePlacement;
        group(&P::meshRenderer, "共通メッシュ描画###Shared mesh",
              [&](auto c)
              {
                  asset(c, &SceneRuntime::MeshRendererComponent::model, "モデル###Model", AssetKind::Model, false);
              });
        group(&P::material, "共通Material割り当て###Shared material",
              [&](auto c)
              {
                  asset(c, &SceneRuntime::MaterialComponent::asset, "Material###Material", AssetKind::Material, true);
              });
        group(&P::rotator, "共通自動回転###Shared rotator",
              [&](auto c)
              {
                  vector(c, &SceneRuntime::RotatorComponent::angularVelocity, "角速度（度/秒）###Angular velocity");
              });
        group(&P::playerController, "共通プレイヤー設定###Shared player",
              [&](auto c)
              {
                  scalar(c, &SceneRuntime::PlayerControllerComponent::moveSpeed, "移動速度###Move speed");
                  scalar(c, &SceneRuntime::PlayerControllerComponent::jumpSpeed, "ジャンプ速度###Jump speed");
                  boolean(c, &SceneRuntime::PlayerControllerComponent::useGravity, "重力を使用###Use gravity");
                  scalar(c, &SceneRuntime::PlayerControllerComponent::gravity, "重力###Gravity");
              });
        group(&P::boxCollider, "共通Collider###Shared collider",
              [&](auto c)
              {
                  vector(c, &SceneRuntime::BoxColliderComponent::center, "中心###Center");
                  vector(c, &SceneRuntime::BoxColliderComponent::size, "サイズ###Size", .001f);
                  boolean(c, &SceneRuntime::BoxColliderComponent::isTrigger, "トリガー###Trigger");
              });
        group(&P::rigidBody, "共通RigidBody###Shared rigidbody",
              [&](auto c)
              {
                  scalar(c, &SceneRuntime::RigidBodyComponent::mass, "質量###Mass", .001f);
                  scalar(c, &SceneRuntime::RigidBodyComponent::friction, "摩擦###Friction", 0, 1);
                  scalar(c, &SceneRuntime::RigidBodyComponent::restitution, "反発###Restitution", 0, 1);
                  scalar(c, &SceneRuntime::RigidBodyComponent::gravityScale, "重力倍率###Gravity scale", -100000);
              });
        group(&P::rectTransform, "共通UI配置###Shared rect",
              [&](auto c)
              {
                  vector(c, &SceneRuntime::RectTransformComponent::position, "位置###Position");
                  vector(c, &SceneRuntime::RectTransformComponent::size, "サイズ###Size", 0);
                  scalar(c, &SceneRuntime::RectTransformComponent::rotation, "回転（ラジアン）###Rotation", -100000);
              });
        ImGui::TextWrapped(
            "全対象にあるコンポーネントを表示します。値が異なる項目は混在表示になり、操作した項目だけを全対象へ反映します。");
        if (changed)
            state.RequestComponentBatch(std::move(objects), state.Interaction());
    }

    auto MultiComponentPanel::SelectedObjects(const SceneRuntime::SceneLayout& layout, const EditState& state)
        -> std::vector<SceneRuntime::ScenePlacement>
    {
        std::vector<SceneRuntime::ScenePlacement> objects;
        for (const auto& object : layout.objects)
            if (state.IsSelected(object.id))
                objects.push_back(object);
        return objects;
    }
}
