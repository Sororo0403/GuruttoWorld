#include "ObjectPanel.h"
#include "PanelLayout.h"
#include <algorithm>
#include <numbers>

namespace Editor
{
    void ObjectPanel::Draw(SceneRuntime::SceneWorld& world, EditState& state, const std::array<float, 3>& suggestedPosition, bool enabled)
    {
        DrawObjects(world, state, enabled);
        DrawInspector(world, state, enabled);
        DrawModels(state, suggestedPosition, enabled);
    }

    void ObjectPanel::DrawObjects(const SceneRuntime::SceneWorld& world, EditState& state, bool enabled)
    {
        const auto& objects = world.Layout().objects;
        PanelLayout::Place(PanelLayout::Panel::Objects);
        if (ImGui::Begin("Hierarchy###Objects"))
        {
            filter_.Draw("Search", -1);
            ImGui::Text("%zu objects", objects.size());
            if (ImGui::BeginChild("Object list", ImVec2(0, 0)))
            {
                ImGui::BeginDisabled(!enabled);
                for (const auto& object : objects)
                {
                    const auto searchable = object.name + " " + object.id + " " + object.model.generic_string();
                    if (!filter_.PassFilter(searchable.c_str())) continue;
                    ImGui::PushID(object.id.c_str());
                    const auto textPosition=ImGui::GetCursorScreenPos();
                    if (ImGui::Selectable("##object", state.SelectedId() == object.id, 0, ImVec2(0, ImGui::GetTextLineHeight())))
                    {
                        state.Select(object.id);
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

    void ObjectPanel::DrawInspector(SceneRuntime::SceneWorld& world, EditState& state, bool enabled)
    {
        const auto& objects = world.Layout().objects;
        PanelLayout::Place(PanelLayout::Panel::Inspector);
        if (ImGui::Begin("Inspector"))
        {
            const auto found = std::find_if(objects.begin(), objects.end(),
                [&](const auto& object) { return object.id == state.SelectedId(); });
            if (found == objects.end()) ImGui::TextUnformatted("Select an object in Hierarchy or Scene.");
            else
            {
                ImGui::PushID(found->id.c_str());
                ImGui::BeginDisabled(!enabled);
                DrawName(world, state, *found);
                ImGui::Text("ID: %s", found->id.c_str());
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

    void ObjectPanel::ScanModels(const std::filesystem::path& root)
    {
        models_.clear();
        catalogError_.clear();
        try
        {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(root / "Assets/Models/Title"))
                if (entry.is_regular_file() && entry.path().extension() == ".obj")
                    models_.push_back(entry.path().lexically_relative(root));
            std::sort(models_.begin(), models_.end());
        }
        catch (const std::exception& error) { catalogError_ = error.what(); }
    }

    void ObjectPanel::DrawModels(EditState& state, const std::array<float, 3>& suggestedPosition, bool enabled)
    {
        if (!positionInitialized_) { addPosition_ = suggestedPosition; positionInitialized_ = true; }
        PanelLayout::Place(PanelLayout::Panel::Models);
        if (ImGui::Begin("Models"))
        {
            modelFilter_.Draw("Search models", -1);
            if (!catalogError_.empty()) ImGui::TextWrapped("%s", catalogError_.c_str());
            ImGui::Text("%zu models", models_.size());
            if (ImGui::BeginChild("Model list", ImVec2(0, 280), true))
            {
                for (const auto& model : models_)
                {
                    const auto path = model.generic_string();
                    if (!modelFilter_.PassFilter(path.c_str())) continue;
                    if (ImGui::Selectable(path.substr(std::string("Assets/Models/Title/").size()).c_str(),
                        model == selectedModel_)) selectedModel_ = model;
                }
            }
            ImGui::EndChild();
            ImGui::DragFloat3("Add position", addPosition_.data(), 0.1f);
            if (ImGui::Button("Use camera front")) addPosition_ = suggestedPosition;
            ImGui::BeginDisabled(!enabled || selectedModel_.empty());
            if (ImGui::Button("Add selected model"))
                state.Request(ObjectRequest{ ObjectAction::Add, {}, selectedModel_, addPosition_ });
            ImGui::EndDisabled();
            ImGui::TextWrapped("New objects use scale 4. Adjust them in Inspector.");
        }
        ImGui::End();
    }
}
