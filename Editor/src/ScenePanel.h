#pragma once
#include "SceneViewport.h"
#include "PanelLayout.h"
#include <imgui.h>
#include <algorithm>
#include <cstdint>

namespace Editor
{
    class ScenePanel final
    {
    public:
        // Keep the Scene window current while constructing its overlays; always pair with End.
        bool Begin(std::uint64_t texture, const char* name = "Scene")
        {
            viewport_ = {};
            hovered_ = false;
            PanelLayout::Place(PanelLayout::Panel::Scene);
            if (!ImGui::Begin(name, nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) return false;
            const auto size = ImGui::GetContentRegionAvail();
            if (size.x < 1 || size.y < 1 || !texture) return false;
            const auto position = ImGui::GetCursorScreenPos();
            ImGui::Image(static_cast<ImTextureID>(texture), size);
            viewport_ = {position.x, position.y, size.x, size.y};
            hovered_ = ImGui::IsItemHovered();
            return true;
        }
        static void End() { ImGui::End(); }
        const SceneViewport& Viewport() const { return viewport_; }
        bool Hovered() const { return hovered_; }
        std::array<unsigned int, 2> RequestedSize() const
        {
            if (!viewport_.Valid()) return {};
            return {static_cast<unsigned int>(std::clamp(viewport_.width, 1.0f, 16384.0f)),
                static_cast<unsigned int>(std::clamp(viewport_.height, 1.0f, 16384.0f))};
        }
    private:
        SceneViewport viewport_;
        bool hovered_ = false;
    };
}
