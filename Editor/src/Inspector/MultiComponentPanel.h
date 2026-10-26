#pragma once
#include <SceneRuntime/Scene/ComponentFieldSchema.h>
#include "../Editing/EditState.h"
#include "../Project/ProjectCatalog.h"
#include <imgui.h>
#include <imgui_internal.h>

namespace Editor
{
    class MultiComponentPanel final
    {
    public:
        template <class Component, class Value>
        static bool Apply(std::vector<SceneRuntime::ScenePlacement>& objects,
                          std::optional<Component> SceneRuntime::ScenePlacement::* component, Value Component::* field,
                          const Value& value)
        {
            if (objects.empty() || std::any_of(objects.begin(), objects.end(),
                                               [&](const auto& object)
                                               {
                                                   return !(object.*component);
                                               }))
                return false;
            for (auto& object : objects)
                ((object.*component).value()).*field = value;
            return true;
        }
        /// <summary>編集画面を描画します。</summary>
        static void Draw(EditState& state, const SceneRuntime::SceneLayout& layout, const ProjectCatalog* catalog);

    private:
        /// <summary>全対象が持つコンポーネントの共通編集を表示します。</summary>
        template <class Member, class Draw>
        static bool DrawGroup(EditState& state, std::vector<SceneRuntime::ScenePlacement>& objects, Member component,
                              const char* title, Draw draw)
        {
            bool changed = false;
            if (std::any_of(objects.begin(), objects.end(),
                            [&](const auto& object)
                            {
                                return !(object.*component);
                            }))
                return false;
            if (!ImGui::CollapsingHeader(title))
                return false;
            ImGui::PushID(title);
            using Component = typename std::remove_reference_t<decltype(objects.front().*component)>::value_type;
            changed |= Field(state, objects, component, &Component::enabled, "有効###Enabled",
                             [](auto& value)
                             {
                                 return ImGui::Checkbox("有効###Enabled", &value);
                             });
            draw(component);
            ImGui::PopID();
            return changed;
        }
        /// <summary>共通アセットの選択を一括編集へ反映します。</summary>
        template <class Member, class Value>
        static bool DrawAsset(EditState& state, std::vector<SceneRuntime::ScenePlacement>& objects, Member component,
                              Value field, const char* label, AssetKind kind, bool empty, const ProjectCatalog* catalog)
        {
            return Field(
                state, objects, component, field, label,
                [&](auto& value)
                {
                    bool edited = false;
                    if (ImGui::BeginCombo(label, ProjectCatalog::Text(value).c_str()))
                    {
                        if (empty && ImGui::Selectable("モデルの設定", value.empty()))
                        {
                            value.clear();
                            edited = true;
                        }
                        if (catalog)
                            for (const auto& entry : catalog->Assets())
                                if (entry.kind == kind &&
                                    ImGui::Selectable(ProjectCatalog::Text(entry.path).c_str(), entry.path == value))
                                {
                                    value = entry.path;
                                    edited = true;
                                }
                        ImGui::EndCombo();
                    }
                    return edited;
                });
        }
        /// <summary>共通の数値フィールドをスキーマの範囲内で編集します。</summary>
        template <class Member, class Value>
        static bool DrawScalar(EditState& state, std::vector<SceneRuntime::ScenePlacement>& objects, Member component,
                               Value field, const char* label, float low, float high)
        {
            if constexpr (std::is_same_v<decltype(field), float SceneRuntime::PlayerControllerComponent::*>)
            {
                const auto& schema = SceneRuntime::PlayerControllerField(field);
                low = schema.minimum;
                high = schema.maximum;
            }
            return Field(state, objects, component, field, label,
                         [&](auto& value)
                         {
                             return ImGui::DragFloat(label, &value, .05f, low, high, "%.3f",
                                                     ImGuiSliderFlags_AlwaysClamp);
                         });
        }
        /// <summary>選択中の配置を収集します。</summary>
        static std::vector<SceneRuntime::ScenePlacement> SelectedObjects(const SceneRuntime::SceneLayout& layout,
                                                                         const EditState& state);

        template <class Component, class Value, class Widget>
        static bool Field(EditState& state, std::vector<SceneRuntime::ScenePlacement>& objects,
                          std::optional<Component> SceneRuntime::ScenePlacement::* component, Value Component::* field,
                          const char* label, Widget draw)
        {
            auto value = ((objects.front().*component).value()).*field;
            const bool mixed = std::any_of(objects.begin(), objects.end(),
                                           [&](const auto& object)
                                           {
                                               return ((object.*component).value()).*field != value;
                                           });
            ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, mixed);
            const bool changed = draw(value);
            ImGui::PopItemFlag();
            if (ImGui::IsItemActive() || ImGui::IsItemDeactivatedAfterEdit())
                state.SetInteraction("multi/component/" + std::to_string(ImGui::GetItemID()));
            if (mixed && ImGui::IsItemHovered())
                ImGui::SetTooltip("複数の値が混在しています：%s", label);
            if (mixed)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("混在");
            }
            if (changed)
                Apply(objects, component, field, value);
            return changed;
        }
    };
}
