#include "ScenePanel.h"

namespace Editor
{
    auto ScenePanel::Begin(std::uint64_t texture, const char* name) -> bool
    {
        viewport_ = {};
        hovered_ = false;
        focused_ = false;
        PanelLayout::Place(PanelLayout::Panel::Scene);
        if (!ImGui::Begin(name, nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
            return false;
        focused_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        const auto size = ImGui::GetContentRegionAvail();
        if (size.x < 1 || size.y < 1 || !texture)
            return false;
        const auto position = ImGui::GetCursorScreenPos();
        ImGui::Image(static_cast<ImTextureID>(texture), size);
        viewport_ = {position.x, position.y, size.x, size.y};
        hovered_ = ImGui::IsItemHovered();
        return true;
    }

    auto ScenePanel::RequestedSize() const -> std::array<unsigned int, 2>
    {
        if (!viewport_.Valid())
            return {};
        return {static_cast<unsigned int>(std::clamp(viewport_.width, 1.0f, 16384.0f)),
                static_cast<unsigned int>(std::clamp(viewport_.height, 1.0f, 16384.0f))};
    }
}
