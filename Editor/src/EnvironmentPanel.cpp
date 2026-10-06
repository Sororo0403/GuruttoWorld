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
    void Vector2(Editor::EditState& state, const char* label, std::array<float,2>& value, float low, float high)
    {
        ImGui::DragFloat2(label,value.data(),0.001f,low,high,"%.3f",ImGuiSliderFlags_AlwaysClamp); Track(state);
    }
    void Sky(Editor::EditState& state, SceneRuntime::SkyComponent& sky)
    {
        Color(state,"Horizon color",sky.horizon); Color(state,"Zenith color",sky.zenith);
        Scalar(state,"Horizon height",sky.horizonHeight,0.001f,2);
        Color(state,"Sun color",sky.sunColor);
        Vector2(state,"Sun center",sky.sunCenter,-10,10); Vector2(state,"Sun radius",sky.sunRadius,0.001f,10);
        Scalar(state,"Sun strength",sky.sunStrength,0,1);
        Color(state,"Cloud bottom color",sky.cloudLow); Color(state,"Cloud top color",sky.cloudHigh);
        Scalar(state,"Cloud opacity",sky.cloudOpacity,0,1);
        Vector2(state,"Cloud velocity (UV/s)",sky.cloudVelocity,-10,10);
        Scalar(state,"Sky reference aspect",sky.referenceAspect,0.01f,100);
        for (size_t index=0;index<sky.clouds.size();++index)
        {
            ImGui::PushID(static_cast<int>(index));
            ImGui::Text("Cloud bank %zu",index+1);
            Vector2(state,"Center (UV)",sky.clouds[index].center,-10,10);
            Scalar(state,"Size",sky.clouds[index].size,0,10);
            ImGui::PopID();
        }
        ImGui::TextWrapped("The first enabled Sky supplies the background. Transform does not move this screen-space sky.");
    }
    void Emitter(Editor::EditState& state, SceneRuntime::ParticleEmitterComponent& emitter)
    {
        int count=static_cast<int>(emitter.count);
        ImGui::DragInt("Particle count",&count,1,0,1024,"%d",ImGuiSliderFlags_AlwaysClamp); Track(state);
        emitter.count=static_cast<unsigned int>(count);
        ImGui::ColorEdit4("Particle color / opacity",emitter.color.data()); Track(state);
        Scalar(state,"Size (world units)",emitter.size,0.001f,1000);
        Vector(state,"Local spawn extent",emitter.extent,0,100000);
        Vector(state,"Local travel per cycle",emitter.travel,-100000,100000);
        Scalar(state,"Cycle (seconds)",emitter.cycle,0.01f,100000);
        Scalar(state,"Local horizontal drift",emitter.drift,0,100000);
        ImGui::TextWrapped("Transform sets the local spawn origin. Parent transforms move the emission volume. Play animates deterministic glow particles.");
    }
    void Sway(Editor::EditState& state, SceneRuntime::CameraSwayComponent& sway)
    {
        Vector(state,"Local sway amplitude",sway.amplitude,-100000,100000);
        Vector(state,"Period (seconds)",sway.period,0.01f,100000);
        ImGui::TextWrapped("Add Camera to this object. Play offsets its view without modifying the saved Transform.");
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
        Component(state,placement.sky,"Sky",[&](auto& value) { Sky(state,value); });
        Component(state,placement.particleEmitter,"ParticleEmitter",[&](auto& value) { Emitter(state,value); });
        Component(state,placement.cameraSway,"CameraSway",[&](auto& value) { Sway(state,value); });
        return !placement.SameComponents(before);
    }
    bool EnvironmentPanel::Add(SceneRuntime::ScenePlacement& placement)
    {
        bool changed=false;
        if (ImGui::MenuItem("Camera",nullptr,false,!placement.camera))
        { const auto id=NewId(placement,"camera"); placement.camera.emplace(); placement.camera->id=id; changed=true; }
        if (ImGui::MenuItem("DirectionalLight",nullptr,false,!placement.directionalLight))
        { const auto id=NewId(placement,"light"); placement.directionalLight.emplace(); placement.directionalLight->id=id; changed=true; }
        if (ImGui::MenuItem("Sky",nullptr,false,!placement.sky))
        { const auto id=NewId(placement,"sky"); placement.sky.emplace(); placement.sky->id=id; changed=true; }
        if (ImGui::MenuItem("ParticleEmitter",nullptr,false,!placement.particleEmitter))
        { const auto id=NewId(placement,"particles"); placement.particleEmitter.emplace(); placement.particleEmitter->id=id; changed=true; }
        if (ImGui::MenuItem("CameraSway",nullptr,false,!placement.cameraSway))
        { const auto id=NewId(placement,"sway"); placement.cameraSway.emplace(); placement.cameraSway->id=id; changed=true; }
        return changed;
    }
}
