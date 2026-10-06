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
            if (ImGui::Begin("コンソール###Console")) DrawContents();
            ImGui::End();
        }
    private:
        void DrawContents()
        {
            if (ImGui::Button("クリア###Clear")) Engine::Log::ClearRecent();
            ImGui::SameLine();
            ImGui::Checkbox("自動スクロール###Auto-scroll",&autoScroll_);
            const char* names[]{"デバッグ###Debug","情報###Info","警告###Warning","エラー###Error"};
            for (size_t index=0;index<levels_.size();++index)
            {
                ImGui::SameLine();
                ImGui::Checkbox(names[index],&levels_[index]);
            }
            ImGui::InputText("ログを検索###Search logs",search_.data(),search_.size());
            const auto entries=Engine::Log::Recent();
            ImGui::Text("最近のログ：%zu / 500件",entries.size());
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
            if (!visible) ImGui::TextUnformatted("一致するログはありません。");
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
