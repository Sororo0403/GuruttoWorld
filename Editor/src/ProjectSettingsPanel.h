#pragma once
#include "ProjectCatalog.h"
#include "InputBindingsPanel.h"
#include <SceneRuntime/ProjectSettings.h>
#include <imgui.h>

namespace Editor
{
    class ProjectSettingsPanel
    {
    public:
        bool TakeSaved() { const bool result=saved_; saved_=false; return result; }
        void Open(const std::filesystem::path& root)
        {
            loaded_=false;
            try { settings_=SceneRuntime::ProjectSettings::Load(root); error_.clear(); loaded_=true; }
            catch(const std::exception& exception) { error_=exception.what(); }
            title_.fill(0); std::copy(settings_.title.begin(),settings_.title.end(),title_.begin());
            requested_=true;
        }
        void Draw(const std::filesystem::path& root,const ProjectCatalog& catalog)
        {
            if(requested_) {requested_=false; ImGui::OpenPopup("プロジェクト設定###Project settings");}
            if(!ImGui::BeginPopupModal("プロジェクト設定###Project settings",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) return;
            ImGui::BeginDisabled(!loaded_);
            ImGui::InputText("ウィンドウタイトル###Window title",title_.data(),title_.size());
            if(ImGui::BeginCombo("起動シーン###Startup scene",settings_.startupScene.c_str())) {
                for(const auto& asset:catalog.Assets()) if(asset.kind==AssetKind::Scene) {
                    const auto path=ProjectCatalog::Text(asset.path);
                    if(ImGui::Selectable(path.c_str(),path==settings_.startupScene)) settings_.startupScene=path;
                }
                ImGui::EndCombo();
            }
            ImGui::InputInt("幅###Window width",&settings_.width);
            ImGui::InputInt("高さ###Window height",&settings_.height);
            InputBindingsPanel::Draw(settings_.inputActions);
            ImGui::TextUnformatted("入力設定はEditorの再生へ即時反映し、Appはビルド後の起動に反映します。");
            if(!error_.empty()) ImGui::TextWrapped("%s",error_.c_str());
            if(ImGui::Button("保存###Save project settings")) {
                try { settings_.title=title_.data(); settings_.Save(root); saved_=true; ImGui::CloseCurrentPopup(); }
                catch(const std::exception& exception) { error_=exception.what(); }
            }
            ImGui::EndDisabled();
            ImGui::SameLine(); if(ImGui::Button("キャンセル###Cancel project settings")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    private:
        SceneRuntime::ProjectSettings settings_;
        std::array<char,1025> title_{};
        std::string error_;
        bool requested_=false,saved_=false;
        bool loaded_=false;
    };
}
