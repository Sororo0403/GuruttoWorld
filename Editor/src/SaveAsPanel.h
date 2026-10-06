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
        bool Requested() const { return requested_; }
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
            if (requested_) { ImGui::OpenPopup("シーンを別名で保存###Save scene as"); requested_=false; }
            if (!ImGui::BeginPopupModal("シーンを別名で保存###Save scene as",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) return;
            ImGui::TextUnformatted("Assets/Scenes内のファイル名：");
            ImGui::InputText("ファイル名###Filename",filename_.data(),filename_.size());
            bool saved=false;
            if (ImGui::Button("保存###Save"))
            {
                try
                {
                    const auto target=SceneDocument::SaveTarget(root,filename_.data());
                    if (std::filesystem::exists(target)) { target_=target; ImGui::OpenPopup("シーンを上書きしますか？###Overwrite scene?"); }
                    else { saved=save(target,false); if (!saved) error_=status; }
                }
                catch (const std::exception& exception) { error_=exception.what(); }
            }
            ImGui::SameLine();
            if (ImGui::Button("キャンセル###Cancel")) { target_.reset(); ImGui::CloseCurrentPopup(); }
            saved=DrawOverwrite(save,status) || saved;
            if (saved) { target_.reset(); ImGui::CloseCurrentPopup(); }
            if (!error_.empty()) ImGui::TextWrapped("%s",error_.c_str());
            ImGui::EndPopup();
        }
    private:
        template<class Save> bool DrawOverwrite(Save save, const std::string& status)
        {
            if (!ImGui::BeginPopupModal("シーンを上書きしますか？###Overwrite scene?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) return false;
            bool saved=false;
            if (target_)
            {
                ImGui::TextWrapped("このシーンを上書きしますか？\n%s",ProjectCatalog::Text(*target_).c_str());
                if (ImGui::Button("上書き###Overwrite"))
                {
                    saved=save(*target_,true);
                    if (saved) ImGui::CloseCurrentPopup(); else error_=status;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("キャンセル###Cancel")) { target_.reset(); ImGui::CloseCurrentPopup(); }
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
