#pragma once
#include <imgui.h>
#include <algorithm>

namespace Editor::PanelLayout
{
    enum class Panel { Commands, Objects, Inspector, Gizmo, Models, Camera };
    inline bool requested=true, apply=false;
    inline void Reset() { requested=true; }
    inline void BeginFrame() { apply=requested; requested=false; }
    inline void Place(Panel panel)
    {
        const auto screen=ImGui::GetIO().DisplaySize;
        const float width=std::clamp(screen.x*0.24f,200.0f,320.0f);
        const float height=std::max(200.0f,screen.y-16.0f);
        const float commands=std::min(270.0f,height*0.46f);
        const float inspector=height*0.54f;
        const float centerWidth=std::clamp(screen.x-2*width-32,200.0f,440.0f);
        ImVec2 position(8,8), size(width,commands);
        bool collapsed=false;
        switch (panel)
        {
        case Panel::Objects: position.y+=commands+8; size.y=height-commands-8; break;
        case Panel::Inspector: position.x=screen.x-width-8; size.y=inspector; break;
        case Panel::Gizmo: position={screen.x-width-8,16+inspector}; size.y=height-inspector-8; break;
        case Panel::Models: position={width+16,8}; size={centerWidth,std::min(520.0f,height)}; collapsed=true; break;
        case Panel::Camera: position={width+16,40}; size={centerWidth,210}; collapsed=true; break;
        default: break;
        }
        const auto condition=apply ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
        ImGui::SetNextWindowPos(position,condition);
        ImGui::SetNextWindowSize(size,condition);
        ImGui::SetNextWindowCollapsed(collapsed,condition);
    }
}
