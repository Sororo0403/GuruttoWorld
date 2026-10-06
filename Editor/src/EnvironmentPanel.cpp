#include "EnvironmentPanel.h"
#include <imgui.h>

namespace
{
    void Track(Editor::EditState& state)
    {
        if (ImGui::IsItemActive() || ImGui::IsItemDeactivatedAfterEdit())
            state.SetInteraction("component/"+std::to_string(ImGui::GetItemID()));
    }
    void Scalar(Editor::EditState& state, const char* label, float& value, float low, float high)
    {
        ImGui::DragFloat(label,&value,0.01f,low,high,"%.3f",ImGuiSliderFlags_AlwaysClamp); Track(state);
    }
    void Vector(Editor::EditState& state, const char* label, std::array<float,3>& value, float low, float high)
    {
        ImGui::DragFloat3(label,value.data(),0.01f,low,high,"%.3f",ImGuiSliderFlags_AlwaysClamp); Track(state);
    }
    void Color(Editor::EditState& state, const char* label, std::array<float,3>& value)
    {
        ImGui::ColorEdit3(label,value.data()); Track(state);
    }
    template<class T, class Draw>
    void Component(Editor::EditState& state, std::optional<T>& component, const char* name, Draw draw)
    {
        if (!component || !ImGui::CollapsingHeader(name,ImGuiTreeNodeFlags_DefaultOpen)) return;
        ImGui::PushID(component->id.c_str());
        ImGui::Text("Component ID: %s",component->id.c_str());
        ImGui::Checkbox("Enabled",&component->enabled);
        draw(*component);
        if (ImGui::Button("Reset")) { const auto id=component->id; component=T{}; component->id=id; }
        ImGui::SameLine();
        if (ImGui::Button("Remove")) component.reset();
        ImGui::PopID();
        Track(state);
    }
    void Camera(Editor::EditState& state, SceneRuntime::CameraComponent& camera)
    {
        Scalar(state,"Vertical FOV (deg)",camera.verticalFov,1,175);
        Scalar(state,"Near clip",camera.nearClip,0.001f,camera.farClip-0.001f);
        Scalar(state,"Far clip",camera.farClip,camera.nearClip+0.001f,1000000);
        Scalar(state,"Reference aspect",camera.referenceAspect,0.01f,100);
        ImGui::Checkbox("Keep horizontal framing on narrow screens",&camera.preserveHorizontal);
    }
    void Light(Editor::EditState& state, SceneRuntime::DirectionalLightComponent& light)
    {
        Vector(state,"Local direction",light.direction,-100000,100000);
        Color(state,"Light color",light.color);
        Scalar(state,"Intensity",light.intensity,0,100);
        Scalar(state,"Ambient intensity",light.ambient,0,100);
        Scalar(state,"Specular strength",light.specular,0,100);
        Scalar(state,"Shininess",light.shininess,1,10000);
        ImGui::TextWrapped("The first enabled DirectionalLight in Hierarchy supplies scene lighting.");
    }
}
namespace Editor
{
    std::string EnvironmentPanel::NewId(const SceneRuntime::ScenePlacement& placement, const std::string& base)
    {
        std::string id=base;
        size_t counter=2;
        while (placement.HasComponentId(id)) id=base+"-"+std::to_string(counter++);
        return id;
    }
    bool EnvironmentPanel::Draw(EditState& state, SceneRuntime::ScenePlacement& placement)
    {
        const auto before=placement;
        Component(state,placement.camera,"Camera",[&](auto& value) { Camera(state,value); });
        Component(state,placement.directionalLight,"DirectionalLight",[&](auto& value) { Light(state,value); });
        return !placement.SameComponents(before);
    }
    bool EnvironmentPanel::Add(SceneRuntime::ScenePlacement& placement)
    {
        bool changed=false;
        if (ImGui::MenuItem("Camera",nullptr,false,!placement.camera))
        { const auto id=NewId(placement,"camera"); placement.camera.emplace(); placement.camera->id=id; changed=true; }
        if (ImGui::MenuItem("DirectionalLight",nullptr,false,!placement.directionalLight))
        { const auto id=NewId(placement,"light"); placement.directionalLight.emplace(); placement.directionalLight->id=id; changed=true; }
        return changed;
    }
}
