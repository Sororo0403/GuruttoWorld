#include "ProjectPanel.h"
#include "PanelLayout.h"
#include "ModelDrop.h"

namespace Editor
{
    void ProjectPanel::Scan(const std::filesystem::path& root)
    {
        root_=root;
        if (!catalog_.Scan(root)) return;
        if (std::none_of(catalog_.Assets().begin(),catalog_.Assets().end(),
            [&](const auto& asset) { return asset.path==selected_; })) selected_.clear();
        if (std::find(catalog_.Folders().begin(),catalog_.Folders().end(),folder_)==catalog_.Folders().end()) folder_="Assets";
    }
    void ProjectPanel::Draw(EditState& state, const std::array<float,3>& suggestedPosition, bool enabled)
    {
        if (!positionInitialized_) { addPosition_=suggestedPosition; positionInitialized_=true; }
        PanelLayout::Place(PanelLayout::Panel::Models);
        if (ImGui::Begin("Project###Models"))
        {
            if (ImGui::Button("Refresh")) Scan(root_);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(180);
            ImGui::InputText("Search assets",search_.data(),search_.size());
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            ImGui::Combo("Type",&type_,"All\0Models\0Scenes\0");
            if (!catalog_.Error().empty()) ImGui::TextWrapped("%s",catalog_.Error().c_str());
            const float listHeight=std::max(70.0f,ImGui::GetContentRegionAvail().y-130);
            if (ImGui::BeginChild("Folders",ImVec2(180,listHeight),ImGuiChildFlags_Borders)) DrawFolder("Assets");
            ImGui::EndChild();
            ImGui::SameLine();
            if (ImGui::BeginChild("Assets",ImVec2(0,listHeight),ImGuiChildFlags_Borders)) DrawAssets(enabled);
            ImGui::EndChild();
            DrawSelection(state,suggestedPosition,enabled);
        }
        ImGui::End();
    }
    void ProjectPanel::DrawFolder(const std::filesystem::path& folder)
    {
        const auto path=ProjectCatalog::Text(folder);
        const auto label=ProjectCatalog::Text(folder.filename());
        const bool children=std::any_of(catalog_.Folders().begin(),catalog_.Folders().end(),
            [&](const auto& candidate) { return candidate.parent_path()==folder; });
        auto flags=ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (!children) flags |= ImGuiTreeNodeFlags_Leaf;
        if (folder==folder_) flags |= ImGuiTreeNodeFlags_Selected;
        if (folder=="Assets") flags |= ImGuiTreeNodeFlags_DefaultOpen;
        ImGui::PushID(path.c_str());
        const bool open=ImGui::TreeNodeEx("folder",flags,"%s",label.c_str());
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) folder_=folder;
        if (open)
        {
            for (const auto& candidate : catalog_.Folders()) if (candidate.parent_path()==folder) DrawFolder(candidate);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    void ProjectPanel::DrawAssets(bool enabled)
    {
        ImGui::TextUnformatted(search_[0] ? "Search results (all folders)" : ProjectCatalog::Text(folder_).c_str());
        size_t count=0;
        for (const auto& asset : catalog_.Assets())
        {
            if (!ProjectCatalog::Matches(asset,folder_,search_.data())) continue;
            if ((type_==1 && asset.kind!=AssetKind::Model) || (type_==2 && asset.kind!=AssetKind::Scene)) continue;
            const auto path=ProjectCatalog::Text(asset.path);
            const auto label=search_[0] ? path : ProjectCatalog::Text(asset.path.filename());
            ImGui::PushID(path.c_str());
            const auto position=ImGui::GetCursorScreenPos();
            if (ImGui::Selectable("##asset",asset.path==selected_,0,ImVec2(0,ImGui::GetTextLineHeight()))) selected_=asset.path;
            ImGui::GetWindowDrawList()->AddText(position,ImGui::GetColorU32(ImGuiCol_Text),label.c_str());
            if (enabled && asset.kind==AssetKind::Model && ImGui::BeginDragDropSource())
            {
                ImGui::SetDragDropPayload(ModelPayload,path.c_str(),path.size()+1);
                ImGui::TextUnformatted(label.c_str());
                ImGui::EndDragDropSource();
            }
            ImGui::PopID();
            ++count;
        }
        if (!count) ImGui::TextUnformatted("No matching assets.");
    }
    void ProjectPanel::RequestDrop(EditState& state, const std::string& path, const std::array<float,3>& position) const
    {
        const auto found=std::find_if(catalog_.Assets().begin(),catalog_.Assets().end(),
            [&](const auto& asset) { return asset.kind==AssetKind::Model && ProjectCatalog::Text(asset.path)==path; });
        if (found!=catalog_.Assets().end()) state.Request({ObjectAction::Add,{},found->path,position});
    }
    void ProjectPanel::DrawSelection(EditState& state, const std::array<float,3>& suggestedPosition, bool enabled)
    {
        const auto found=std::find_if(catalog_.Assets().begin(),catalog_.Assets().end(),
            [&](const auto& asset) { return asset.path==selected_; });
        if (found==catalog_.Assets().end()) { ImGui::TextUnformatted("Select a model or scene asset."); return; }
        ImGui::TextWrapped("%s: %s",found->kind==AssetKind::Model ? "Model" : "Scene",ProjectCatalog::Text(selected_).c_str());
        if (found->kind!=AssetKind::Model) return;
        ImGui::DragFloat3("Add position",addPosition_.data(),0.1f);
        if (ImGui::Button("Use camera front")) addPosition_=suggestedPosition;
        ImGui::SameLine();
        ImGui::BeginDisabled(!enabled);
        if (ImGui::Button("Add selected model")) state.Request({ObjectAction::Add,{},selected_,addPosition_});
        ImGui::EndDisabled();
    }
}
