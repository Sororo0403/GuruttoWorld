#include "ObjectPanel.h"
#include "PanelLayout.h"
#include <algorithm>
#include <numbers>

namespace Editor
{
    void ObjectPanel::Draw(SceneRuntime::SceneWorld& world, EditState& state, bool enabled)
    {
        DrawObjects(world, state, enabled);
        DrawInspector(world, state, enabled);
    }

    void ObjectPanel::DrawObjects(const SceneRuntime::SceneWorld& world, EditState& state, bool enabled)
    {
        const auto& objects = world.Layout().objects;
        if (state.SelectedIds().empty()) anchorId_.clear();
        PanelLayout::Place(PanelLayout::Panel::Objects);
        if (ImGui::Begin("Hierarchy###Objects"))
        {
            filter_.Draw("Search", -1);
            ImGui::Text("%zu objects / %zu selected", objects.size(),state.SelectedIds().size());
            std::vector<std::string> visible;
            for (const auto& object : objects)
                if (filter_.PassFilter((object.name+" "+object.id+" "+object.model.generic_string()).c_str())) visible.push_back(object.id);
            if (ImGui::BeginChild("Object list", ImVec2(0, 0)))
            {
                ImGui::BeginDisabled(!enabled);
                for (const auto& object : objects)
                {
                    const auto searchable = object.name + " " + object.id + " " + object.model.generic_string();
                    if (!filter_.PassFilter(searchable.c_str())) continue;
                    ImGui::PushID(object.id.c_str());
                    const auto textPosition=ImGui::GetCursorScreenPos();
                    if (ImGui::Selectable("##object", state.IsSelected(object.id), 0, ImVec2(0, ImGui::GetTextLineHeight())))
                    {
                        SelectObject(state,visible,object.id);
                    }
                    ImGui::GetWindowDrawList()->AddText(textPosition, ImGui::GetColorU32(ImGuiCol_Text), object.name.c_str());
                    ImGui::PopID();
                }
                ImGui::EndDisabled();
            }
            ImGui::EndChild();
        }
        ImGui::End();
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
                ImGui::Text("Parent: %s",found->parentId.empty() ? "<root>" : found->parentId.c_str());
                ImGui::TextWrapped("Model: %s", found->model.generic_string().c_str());
                ImGui::Separator();
                DrawTransform(world, state, *found);
                if (ImGui::Button("Duplicate")) state.Request(ObjectRequest{ ObjectAction::Duplicate, state.SelectedId(), {}, {} });
                ImGui::SameLine();
                if (ImGui::Button("Delete")) state.Request(ObjectRequest{ ObjectAction::Delete, state.SelectedId(), {}, {} });
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
        auto position=found->position;
        ImGui::PushID(found->id.c_str());
        ImGui::BeginDisabled(!enabled);
        if (ImGui::DragFloat3("Active position",position.data(),0.05f,0,0,"%.3f") && position!=found->position)
        {
            const std::array<float,3> delta{position[0]-found->position[0],position[1]-found->position[1],position[2]-found->position[2]};
            state.TranslateSelection(world,delta);
        }
        ImGui::EndDisabled();
        ImGui::PopID();
        ImGui::TextWrapped("Move uses the active object as pivot and preserves spacing. Rotate, scale, duplicate and delete require one object.");
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
        auto position=placement.position, rotation=placement.rotation, scale=placement.scale;
        constexpr float ToDegrees=180.0f/std::numbers::pi_v<float>;
        auto degrees=rotation;
        std::transform(degrees.begin(),degrees.end(),degrees.begin(), [](float value) { return value*ToDegrees; });
        bool edited=ImGui::DragFloat3("Position",position.data(),0.05f,0,0,"%.3f");
        if (ImGui::DragFloat3("Rotation (deg)",degrees.data(),0.5f,0,0,"%.2f"))
        {
            for (size_t i=0;i<3;++i) rotation[i]=degrees[i]/ToDegrees;
            edited=true;
        }
        edited |= ImGui::DragFloat3("Scale",scale.data(),0.05f,0,0,"%.3f");
        if (edited && (position!=placement.position || rotation!=placement.rotation || scale!=placement.scale))
            state.SetTransform(world,placement.id,position,rotation,scale);
        if (ImGui::Button("Reset Transform")) state.ResetTransform(world,placement.id);
    }

}
