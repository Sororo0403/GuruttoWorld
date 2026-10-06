#include "ComponentPanel.h"
#include "EnvironmentPanel.h"
#include <imgui.h>

namespace
{
    std::string NewComponentId(const SceneRuntime::ScenePlacement& placement, const std::string& base)
    {
        std::string id=base;
        size_t counter=2;
        while (placement.HasComponentId(id)) id=base+"-"+std::to_string(counter++);
        return id;
    }
}

namespace Editor
{
    bool ComponentPanel::ChooseModel(std::filesystem::path& model, const ProjectCatalog* catalog)
    {
        if (!catalog) { ImGui::TextUnformatted("Refresh Project to list models."); return false; }
        bool edited=false;
        for (const auto& asset : catalog->Assets())
        {
            if (asset.kind!=AssetKind::Model) continue;
            const auto path=ProjectCatalog::Text(asset.path);
            if (!path.starts_with("Assets/Models/")) continue;
            if (ImGui::Selectable(path.c_str(),asset.path==model)) { model=asset.path; edited=true; }
        }
        return edited;
    }

    bool ComponentPanel::DrawMesh(SceneRuntime::ScenePlacement& candidate, const ProjectCatalog* catalog)
    {
        if (!candidate.meshRenderer || !ImGui::CollapsingHeader("MeshRenderer",ImGuiTreeNodeFlags_DefaultOpen)) return false;
        auto& mesh=*candidate.meshRenderer;
        ImGui::PushID(mesh.id.c_str());
        ImGui::Text("Component ID: %s",mesh.id.c_str());
        bool edited=ImGui::Checkbox("Enabled",&mesh.enabled);
        const auto text=ProjectCatalog::Text(mesh.model);
        if (ImGui::BeginCombo("Model asset",ProjectCatalog::Text(mesh.model.filename()).c_str()))
        { edited=ChooseModel(mesh.model,catalog) || edited; ImGui::EndCombo(); }
        ImGui::TextWrapped("%s",text.c_str());
        if (ImGui::Button("Reset MeshRenderer")) { mesh.enabled=true; edited=true; }
        ImGui::SameLine();
        if (ImGui::Button("Remove MeshRenderer")) { candidate.meshRenderer.reset(); edited=true; }
        ImGui::PopID();
        return edited;
    }

    bool ComponentPanel::DrawRotator(EditState& state, SceneRuntime::ScenePlacement& candidate)
    {
        if (!candidate.rotator || !ImGui::CollapsingHeader("Rotator",ImGuiTreeNodeFlags_DefaultOpen)) return false;
        auto& rotator=*candidate.rotator;
        ImGui::PushID(rotator.id.c_str());
        ImGui::Text("Component ID: %s",rotator.id.c_str());
        bool edited=ImGui::Checkbox("Enabled",&rotator.enabled);
        edited=ImGui::DragFloat3("Angular velocity (deg/s)",rotator.angularVelocity.data(),0.5f,-100000,100000,"%.2f",ImGuiSliderFlags_AlwaysClamp) || edited;
        if (ImGui::IsItemActive() || ImGui::IsItemDeactivatedAfterEdit())
            state.SetInteraction("component/"+std::to_string(ImGui::GetItemID()));
        ImGui::TextUnformatted("Local X / Y / Z rotation during Play");
        if (ImGui::Button("Reset Rotator"))
        { rotator.enabled=true; rotator.angularVelocity={0,90,0}; edited=true; }
        ImGui::SameLine();
        if (ImGui::Button("Remove Rotator")) { candidate.rotator.reset(); edited=true; }
        ImGui::PopID();
        return edited;
    }

    bool ComponentPanel::DrawAdd(SceneRuntime::ScenePlacement& candidate, const ProjectCatalog* catalog)
    {
        if (ImGui::Button("Add Component")) ImGui::OpenPopup("Add component");
        if (!ImGui::BeginPopup("Add component")) return false;
        bool edited=false;
        if (ImGui::BeginMenu("MeshRenderer",!candidate.meshRenderer))
        {
            std::filesystem::path model;
            if (ChooseModel(model,catalog))
            {
                candidate.meshRenderer=SceneRuntime::MeshRendererComponent{NewComponentId(candidate,"mesh"),true,model};
                edited=true; ImGui::CloseCurrentPopup();
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Rotator",nullptr,false,!candidate.rotator))
        {
            candidate.rotator=SceneRuntime::RotatorComponent{NewComponentId(candidate,"rotator"),true,{0,90,0}};
            edited=true;
        }
        edited=EnvironmentPanel::Add(candidate) || edited;
        ImGui::EndPopup();
        return edited;
    }

    void ComponentPanel::Draw(EditState& state, const SceneRuntime::ScenePlacement& placement, const ProjectCatalog* catalog)
    {
        auto candidate=placement;
        bool edited=DrawMesh(candidate,catalog);
        edited=DrawRotator(state,candidate) || edited;
        edited=EnvironmentPanel::Draw(state,candidate) || edited;
        edited=DrawAdd(candidate,catalog) || edited;
        if (edited && !candidate.SameComponents(placement))
            state.RequestComponents(std::move(candidate),state.Interaction());
    }
}
