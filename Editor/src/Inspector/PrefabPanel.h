#pragma once
#include "../Editing/EditState.h"
#include <SceneRuntime/Scene/Prefab.h>
#include <imgui.h>

namespace Editor
{
    /// <summary>Prefabの外部接続と旧形式からの移行操作を表示します。</summary>
    inline void DrawPrefabBindings(EditState& state, const SceneRuntime::SceneLayout& scene,
                                   const SceneRuntime::ScenePlacement& object)
    {
        if (object.id == object.prefab->rootId)
        {
            const auto labels = SceneRuntime::SceneLayout::Parse(object.prefab->baseline).prefabSlots;
            const auto bindings = SceneRuntime::Prefab::ExternalBindings(scene, object.id);
            for (const auto& [slot, target] : bindings)
            {
                ImGui::PushID(slot.c_str());
                const char* preview = target.id.empty() ? "未接続" : target.id.c_str();
                const auto requestBinding = [&](const std::string& id)
                {
                    ObjectRequest request{ObjectAction::BindPrefabReferences, object.id};
                    request.prefabBindings[slot] = {id};
                    state.Request(std::move(request));
                };
                if (ImGui::BeginCombo(
                        ("外部接続：" + (labels.contains(slot) ? labels.at(slot) : slot) + "###" + slot).c_str(),
                        preview))
                {
                    if (ImGui::Selectable("未接続", target.id.empty()))
                        requestBinding("");
                    for (const auto& destination : scene.objects)
                        if (ImGui::Selectable((destination.name + " (" + destination.id + ")").c_str(),
                                              target.id == destination.id))
                            requestBinding(destination.id);
                    ImGui::EndCombo();
                }
                ImGui::PopID();
            }
            if (object.prefab->referenceVersion == 0 && !bindings.empty())
            {
                ImGui::TextUnformatted("旧形式の外部参照です。接続先を選ぶか、未接続へ移行してください。");
                if (ImGui::Button("外部参照を未接続へ移行###Migrate prefab references"))
                {
                    ObjectRequest request{ObjectAction::BindPrefabReferences, object.id};
                    request.prefabBindings = bindings;
                    state.Request(std::move(request));
                }
            }
        }
    }
    inline void DrawPrefabOverrides(EditState& state, const SceneRuntime::SceneLayout& scene,
                                    const SceneRuntime::ScenePlacement& object)
    {
        if (!object.prefab || !ImGui::CollapsingHeader("Prefabの上書き###Prefab overrides"))
            return;
        try
        {
            DrawPrefabBindings(state, scene, object);
            const auto changes = SceneRuntime::Prefab::Overrides(scene, object.id);
            ImGui::Text("変更: %zu", changes.size());
            for (const auto& change : changes)
            {
                ImGui::PushID(change.path.c_str());
                ImGui::TextWrapped("%s", change.path.c_str());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("現在: %s\n元: %s", change.value.c_str(), change.source.c_str());
                if (ImGui::SmallButton("元へ戻す###Revert"))
                {
                    ObjectRequest request{ObjectAction::RevertPrefabProperty, object.id};
                    request.property = change.path;
                    state.Request(std::move(request));
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("元Prefabへ適用###Apply"))
                {
                    ObjectRequest request{ObjectAction::ApplyPrefabProperty, object.id};
                    request.property = change.path;
                    state.Request(std::move(request));
                }
                ImGui::PopID();
            }
        }
        catch (const std::exception& error)
        {
            ImGui::TextWrapped("%s", error.what());
        }
    }
}
