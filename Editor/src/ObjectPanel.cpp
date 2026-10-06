#include "ObjectPanel.h"
#include "PanelLayout.h"
#include <algorithm>
#include <numbers>
#include <iterator>
#include <Engine/Core/Log.h>

namespace
{
    void TrackInspectorEdit(Editor::EditState& state)
    {
        if (ImGui::IsItemActive()) state.SetInteraction("inspector/"+std::to_string(ImGui::GetItemID()));
    }
}

namespace Editor
{
    void ObjectPanel::Draw(SceneRuntime::SceneWorld& world, EditState& state, bool enabled)
    {
        editsEnabled_=enabled;
        DrawObjects(world, state, enabled);
        DrawInspector(world, state, enabled);
    }

    void ObjectPanel::DrawObjects(SceneRuntime::SceneWorld& world, EditState& state, bool enabled)
    {
        if (state.SelectedIds().empty()) anchorId_.clear();
        PanelLayout::Place(PanelLayout::Panel::Objects);
        if (ImGui::Begin("Hierarchy###Objects"))
        {
            filter_.Draw("Search",-1);
            ImGui::Text("%zu objects / %zu selected",world.Layout().objects.size(),state.SelectedIds().size());
            ImGui::BeginDisabled(!enabled);
            ImGui::Selectable("Drop here to make root",false);
            DrawReparentTarget(world,state,{});
            if (!parentError_.empty()) ImGui::TextWrapped("%s",parentError_.c_str());
            if (ImGui::BeginChild("Object list",ImVec2(0,0))) DrawHierarchy(world,state,enabled);
            ImGui::EndChild();
            ImGui::EndDisabled();
        }
        ImGui::End();
    }

    std::vector<HierarchyRow> ObjectPanel::VisibleRows(const SceneRuntime::SceneLayout& layout) const
    {
        if (!filter_.IsActive()) return BuildHierarchyRows(layout,collapsed_);
        std::vector<HierarchyRow> rows;
        for (size_t index=0;index<layout.objects.size();++index)
        {
            const auto& object=layout.objects[index];
            if (filter_.PassFilter((object.name+" "+object.id+" "+object.model.generic_string()).c_str())) rows.push_back({index,0,false});
        }
        return rows;
    }

    void ObjectPanel::DrawHierarchy(SceneRuntime::SceneWorld& world, EditState& state, bool enabled)
    {
        const auto rows=VisibleRows(world.Layout());
        std::vector<std::string> visible;
        std::transform(rows.begin(),rows.end(),std::back_inserter(visible),
            [&](const auto& row) { return world.Layout().objects[row.index].id; });
        ImGui::BeginDisabled(!enabled);
        for (const auto& row : rows) DrawRow(world,state,row,visible);
        ImGui::EndDisabled();
    }

    void ObjectPanel::DrawRow(SceneRuntime::SceneWorld& world, EditState& state,
        const HierarchyRow& row, const std::vector<std::string>& visible)
    {
        const auto& object=world.Layout().objects[row.index];
        const float left=ImGui::GetCursorPosX();
        ImGui::SetCursorPosX(left+static_cast<float>(row.depth)*ImGui::GetTreeNodeToLabelSpacing());
        ImGui::PushID(object.id.c_str());
        auto flags=ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
        if (!row.children) flags |= ImGuiTreeNodeFlags_Leaf;
        if (state.IsSelected(object.id)) flags |= ImGuiTreeNodeFlags_Selected;
        const bool open=ImGui::TreeNodeEx("##object",flags);
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) SelectObject(state,visible,object.id);
        if (row.children)
        {
            if (open) collapsed_.erase(object.id); else collapsed_.insert(object.id);
        }
        auto text=ImGui::GetItemRectMin();
        text.x+=ImGui::GetTreeNodeToLabelSpacing();
        text.y+=(ImGui::GetItemRectSize().y-ImGui::GetTextLineHeight())*0.5f;
        ImGui::GetWindowDrawList()->AddText(text,ImGui::GetColorU32(ImGuiCol_Text),object.name.c_str());
        if (ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload("WP1_HIERARCHY_OBJECT",object.id.c_str(),object.id.size()+1);
            ImGui::TextUnformatted(object.name.c_str());
            ImGui::EndDragDropSource();
        }
        DrawReparentTarget(world,state,object.id);
        ImGui::PopID();
    }

    void ObjectPanel::DrawReparentTarget(SceneRuntime::SceneWorld& world, EditState& state, const std::string& parent)
    {
        if (!editsEnabled_ || !ImGui::BeginDragDropTarget()) return;
        if (const auto* payload=ImGui::AcceptDragDropPayload("WP1_HIERARCHY_OBJECT"))
        {
            const auto* id=static_cast<const char*>(payload->Data);
            if (payload->DataSize>1 && id[payload->DataSize-1]=='\0' &&
                std::char_traits<char>::length(id)==static_cast<size_t>(payload->DataSize-1)) ChangeParent(world,state,id,parent);
        }
        ImGui::EndDragDropTarget();
    }

    void ObjectPanel::ChangeParent(SceneRuntime::SceneWorld& world, EditState& state,
        const std::string& id, const std::string& parent)
    {
        if (!editsEnabled_) return;
        if (!state.SetParent(world,id,parent,parentError_)) Engine::Log::Warning("Reparent: "+parentError_);
    }

    void ObjectPanel::DrawParent(SceneRuntime::SceneWorld& world, EditState& state, const SceneRuntime::ScenePlacement& placement)
    {
        const auto preview=placement.parentId.empty() ? std::string("<root>") : placement.parentId;
        if (ImGui::BeginCombo("Parent",preview.c_str()))
        {
            if (ImGui::Selectable("<root>",placement.parentId.empty())) ChangeParent(world,state,placement.id,{});
            for (const auto& candidate : world.Layout().objects)
            {
                if (candidate.id==placement.id) continue;
                ImGui::PushID(candidate.id.c_str());
                const auto text=ImGui::GetCursorScreenPos();
                if (ImGui::Selectable("##parent",candidate.id==placement.parentId)) ChangeParent(world,state,placement.id,candidate.id);
                ImGui::GetWindowDrawList()->AddText(text,ImGui::GetColorU32(ImGuiCol_Text),candidate.name.c_str());
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if (!parentError_.empty()) ImGui::TextWrapped("%s",parentError_.c_str());
    }

    void ObjectPanel::SelectObject(EditState& state, const std::vector<std::string>& visible, const std::string& id)
    {
        const auto& io=ImGui::GetIO();
        if (io.KeyShift) state.SelectRange(visible,anchorId_,id,io.KeyCtrl);
        else { state.Select(id,io.KeyCtrl); anchorId_=id; }
    }

    void ObjectPanel::DrawInspector(SceneRuntime::SceneWorld& world, EditState& state, bool enabled)
    {
        const auto& objects = world.Layout().objects;
        PanelLayout::Place(PanelLayout::Panel::Inspector);
        if (ImGui::Begin("Inspector"))
        {
            const auto found = std::find_if(objects.begin(), objects.end(),
                [&](const auto& object) { return object.id == state.SelectedId(); });
            if (state.SelectedIds().size()>1)
            {
                DrawMultiInspector(world,state,enabled);
            }
            else if (found == objects.end()) ImGui::TextUnformatted("Select an object in Hierarchy or Scene.");
            else
            {
                ImGui::PushID(found->id.c_str());
                ImGui::BeginDisabled(!enabled);
                DrawName(world, state, *found);
                ImGui::Text("ID: %s", found->id.c_str());
                DrawParent(world,state,*found);
                ImGui::TextWrapped("Model: %s", found->model.generic_string().c_str());
                ImGui::Separator();
                DrawTransform(world, state, *found);
                if (ImGui::Button("Duplicate")) state.Request(state.DuplicateSelectionRequest());
                ImGui::SameLine();
                if (ImGui::Button("Delete")) state.Request(state.DeleteSelectionRequest());
                ImGui::EndDisabled();
                ImGui::PopID();
                if (state.InvalidTransform())
                    ImGui::TextWrapped("Invalid transform. Use finite numbers and nonzero scale.");
            }
            ImGui::Separator();
            ImGui::TextWrapped(state.HasChanges() ? "Unsaved changes." :
                "No unsaved changes.");
        }
        ImGui::End();
    }

    void ObjectPanel::DrawMultiInspector(SceneRuntime::SceneWorld& world, EditState& state, bool enabled)
    {
        ImGui::Text("%zu objects selected",state.SelectedIds().size());
        ImGui::Text("Active ID: %s",state.SelectedId().c_str());
        const auto& objects=world.Layout().objects;
        const auto found=std::find_if(objects.begin(),objects.end(),[&](const auto& object) { return object.id==state.SelectedId(); });
        if (found==objects.end()) return;
        DirectX::XMFLOAT4X4 matrix;
        if (!world.WorldMatrix(found->id,matrix)) return;
        const std::array<float,3> previous{matrix._41,matrix._42,matrix._43};
        auto position=previous;
        ImGui::PushID(found->id.c_str());
        ImGui::BeginDisabled(!enabled);
        if (ImGui::DragFloat3("Active position",position.data(),0.05f,0,0,"%.3f") && position!=previous)
        {
            const std::array<float,3> delta{position[0]-previous[0],position[1]-previous[1],position[2]-previous[2]};
            state.TranslateSelectionWorld(world,delta);
        }
        TrackInspectorEdit(state);
        if (ImGui::Button("Duplicate selected")) state.Request(state.DuplicateSelectionRequest());
        ImGui::SameLine();
        if (ImGui::Button("Delete selected")) state.Request(state.DeleteSelectionRequest());
        ImGui::EndDisabled();
        ImGui::PopID();
        ImGui::TextWrapped("Move uses the active object as pivot and preserves spacing. Rotate and scale require one object. Duplicate and delete use the selection.");
        if (state.InvalidTransform()) ImGui::TextWrapped("Move rejected. Use finite positions within the supported range.");
    }

    void ObjectPanel::DrawName(SceneRuntime::SceneWorld& world, EditState& state,
        const SceneRuntime::ScenePlacement& placement)
    {
        if (inspectedId_!=placement.id || observedName_!=placement.name)
        {
            inspectedId_=placement.id;
            observedName_=placement.name;
            nameBuffer_.assign(placement.name.begin(), placement.name.end());
            nameBuffer_.resize(std::max(size_t{1024},placement.name.size()+256), '\0');
            invalidName_=false;
        }
        if (ImGui::InputText("Name", nameBuffer_.data(), nameBuffer_.size()))
        {
            invalidName_=!state.Rename(world,placement.id,nameBuffer_.data());
            if (!invalidName_) observedName_=placement.name;
        }
        TrackInspectorEdit(state);
        if (ImGui::IsItemDeactivatedAfterEdit() && invalidName_)
        {
            observedName_.clear(); // Restore the last valid name on the next frame.
        }
        if (invalidName_) ImGui::TextWrapped("Name must contain a non-whitespace character.");
    }

    void ObjectPanel::DrawTransform(SceneRuntime::SceneWorld& world, EditState& state,
        const SceneRuntime::ScenePlacement& placement)
    {
        if (!ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) return;
        ImGui::TextUnformatted("Coordinates: Local (relative to parent)");
        auto position=placement.position, rotation=placement.rotation, scale=placement.scale;
        constexpr float ToDegrees=180.0f/std::numbers::pi_v<float>;
        auto degrees=rotation;
        std::transform(degrees.begin(),degrees.end(),degrees.begin(), [](float value) { return value*ToDegrees; });
        bool edited=ImGui::DragFloat3("Local position",position.data(),0.05f,0,0,"%.3f");
        TrackInspectorEdit(state);
        if (ImGui::DragFloat3("Local rotation (deg)",degrees.data(),0.5f,0,0,"%.2f"))
        {
            for (size_t i=0;i<3;++i) rotation[i]=degrees[i]/ToDegrees;
            edited=true;
        }
        TrackInspectorEdit(state);
        edited |= ImGui::DragFloat3("Local scale",scale.data(),0.05f,0,0,"%.3f");
        TrackInspectorEdit(state);
        if (edited && (position!=placement.position || rotation!=placement.rotation || scale!=placement.scale))
            state.SetLocalTransform(world,placement.id,position,rotation,scale);
        if (ImGui::Button("Reset Transform")) state.ResetTransform(world,placement.id);
    }

}
