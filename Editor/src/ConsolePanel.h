#pragma once
#include "ConsoleFilter.h"
#include <imgui.h>

namespace Editor
{
    class ConsolePanel final
    {
    public:
        void Draw()
        {
            if (ImGui::Begin("Console")) DrawContents();
            ImGui::End();
        }
    private:
        void DrawContents()
        {
            if (ImGui::Button("Clear")) Engine::Log::ClearRecent();
            ImGui::SameLine();
            ImGui::Checkbox("Auto-scroll",&autoScroll_);
            const char* names[]{"Debug","Info","Warning","Error"};
            for (size_t index=0;index<levels_.size();++index)
            {
                ImGui::SameLine();
                ImGui::Checkbox(names[index],&levels_[index]);
            }
            ImGui::InputText("Search logs",search_.data(),search_.size());
            const auto entries=Engine::Log::Recent();
            ImGui::Text("%zu / 500 recent entries",entries.size());
            if (ImGui::BeginChild("Log entries",ImVec2(0,0),ImGuiChildFlags_Borders)) DrawEntries(entries);
            ImGui::EndChild();
        }
        void DrawEntries(const std::vector<Engine::LogEntry>& entries)
        {
            const bool atBottom=ImGui::GetScrollY()>=ImGui::GetScrollMaxY()-1;
            size_t visible=0;
            for (const auto& entry : entries)
            {
                if (!ConsoleMatches(entry,levels_,search_.data())) continue;
                const auto color=entry.level==Engine::LogLevel::Error ? ImVec4(1,0.4f,0.4f,1) :
                    entry.level==Engine::LogLevel::Warning ? ImVec4(1,0.8f,0.3f,1) : ImGui::GetStyleColorVec4(ImGuiCol_Text);
                ImGui::PushStyleColor(ImGuiCol_Text,color);
                ImGui::PushTextWrapPos(0);
                ImGui::TextUnformatted(entry.text.c_str());
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
                ++visible;
            }
            if (!visible) ImGui::TextUnformatted("No matching logs.");
            const auto sequence=entries.empty() ? 0 : entries.back().sequence;
            if (autoScroll_ && atBottom && sequence!=lastSequence_) ImGui::SetScrollHereY(1);
            lastSequence_=sequence;
        }
        std::array<bool,4> levels_{true,true,true,true};
        std::array<char,256> search_{};
        bool autoScroll_=true;
        std::uint64_t lastSequence_=0;
    };
}
