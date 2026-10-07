#pragma once
#include "EditState.h"
#include <SceneRuntime/Prefab.h>
#include <imgui.h>

namespace Editor
{
    inline void DrawPrefabOverrides(EditState& state,const SceneRuntime::SceneLayout& scene,const SceneRuntime::ScenePlacement& object)
    {
        if (!object.prefab || !ImGui::CollapsingHeader("Prefabの上書き###Prefab overrides")) return;
        try
        {
            const auto changes=SceneRuntime::Prefab::Overrides(scene,object.id);
            ImGui::Text("変更: %zu",changes.size());
            for (const auto& change : changes)
            {
                ImGui::PushID(change.path.c_str());
                ImGui::TextWrapped("%s",change.path.c_str());
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("現在: %s\n元: %s",change.value.c_str(),change.source.c_str());
                if (ImGui::SmallButton("元へ戻す###Revert"))
                { ObjectRequest request{ObjectAction::RevertPrefabProperty,object.id}; request.property=change.path; state.Request(std::move(request)); }
                ImGui::SameLine();
                if (ImGui::SmallButton("元Prefabへ適用###Apply"))
                { ObjectRequest request{ObjectAction::ApplyPrefabProperty,object.id}; request.property=change.path; state.Request(std::move(request)); }
                ImGui::PopID();
            }
        }
        catch (const std::exception& error) { ImGui::TextWrapped("%s",error.what()); }
    }
}
