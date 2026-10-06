#pragma once
#include "SceneDocument.h"
#include "ProjectCatalog.h"
#include <imgui.h>
#include <array>
#include <algorithm>

namespace Editor
{
    class SaveAsPanel final
    {
    public:
        void Request(const std::filesystem::path& current)
        {
            const auto name=ProjectCatalog::Text(current.filename());
            filename_.fill(0);
            std::copy_n(name.data(),std::min(name.size(),filename_.size()-1),filename_.data());
            requested_=true;
            target_.reset();
            error_.clear();
        }
        template<class Save> void Draw(const std::filesystem::path& root, Save save, const std::string& status)
        {
            if (requested_) { ImGui::OpenPopup("Save scene as"); requested_=false; }
            if (!ImGui::BeginPopupModal("Save scene as",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) return;
            ImGui::TextUnformatted("Filename in Assets/Scenes:");
            ImGui::InputText("Filename",filename_.data(),filename_.size());
            bool saved=false;
            if (ImGui::Button("Save"))
            {
                try
                {
                    const auto target=SceneDocument::SaveTarget(root,filename_.data());
                    if (std::filesystem::exists(target)) { target_=target; ImGui::OpenPopup("Overwrite scene?"); }
                    else { saved=save(target,false); if (!saved) error_=status; }
                }
                catch (const std::exception& exception) { error_=exception.what(); }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) { target_.reset(); ImGui::CloseCurrentPopup(); }
            saved=DrawOverwrite(save,status) || saved;
            if (saved) { target_.reset(); ImGui::CloseCurrentPopup(); }
            if (!error_.empty()) ImGui::TextWrapped("%s",error_.c_str());
            ImGui::EndPopup();
        }
    private:
        template<class Save> bool DrawOverwrite(Save save, const std::string& status)
        {
            if (!ImGui::BeginPopupModal("Overwrite scene?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) return false;
            bool saved=false;
            if (target_)
            {
                ImGui::TextWrapped("Replace this scene?\n%s",ProjectCatalog::Text(*target_).c_str());
                if (ImGui::Button("Overwrite"))
                {
                    saved=save(*target_,true);
                    if (saved) ImGui::CloseCurrentPopup(); else error_=status;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) { target_.reset(); ImGui::CloseCurrentPopup(); }
            if (!error_.empty()) ImGui::TextWrapped("%s",error_.c_str());
            ImGui::EndPopup();
            return saved;
        }
        std::array<char,256> filename_{};
        std::optional<std::filesystem::path> target_;
        std::string error_;
        bool requested_=false;
    };
}
