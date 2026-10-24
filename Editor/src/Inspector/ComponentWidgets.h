#pragma once
#include "../Editing/EditState.h"
#include <imgui.h>
namespace Editor
{
    inline void TrackComponentInteraction(EditState& state)
    {
        if (ImGui::IsItemActive() || ImGui::IsItemDeactivatedAfterEdit())
            state.SetInteraction("component/" + std::to_string(ImGui::GetItemID()));
    }
}
