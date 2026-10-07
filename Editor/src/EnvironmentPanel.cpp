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
        ImGui::Text("コンポーネントID：%s",component->id.c_str());
        ImGui::Checkbox("有効###Enabled",&component->enabled);
        draw(*component);
        if (ImGui::Button("リセット###Reset")) { const auto id=component->id; component=T{}; component->id=id; }
        ImGui::SameLine();
        if (ImGui::Button("削除###Remove")) component.reset();
        ImGui::PopID();
        Track(state);
    }
    void Camera(Editor::EditState& state, SceneRuntime::CameraComponent& camera)
    {
        Scalar(state,"垂直視野角（度）###Vertical FOV (deg)",camera.verticalFov,1,175);
        Scalar(state,"近クリップ距離###Near clip",camera.nearClip,0.001f,camera.farClip-0.001f);
        Scalar(state,"遠クリップ距離###Far clip",camera.farClip,camera.nearClip+0.001f,1000000);
        Scalar(state,"基準アスペクト比###Reference aspect",camera.referenceAspect,0.01f,100);
        ImGui::Checkbox("縦長画面でも横方向の構図を維持###Keep horizontal framing on narrow screens",&camera.preserveHorizontal);
    }
    void Vector2(Editor::EditState& state, const char* label, std::array<float,2>& value, float low, float high)
    {
        ImGui::DragFloat2(label,value.data(),0.001f,low,high,"%.3f",ImGuiSliderFlags_AlwaysClamp); Track(state);
    }
    void Sky(Editor::EditState& state, SceneRuntime::SkyComponent& sky)
    {
        Color(state,"地平線の色###Horizon color",sky.horizon); Color(state,"天頂の色###Zenith color",sky.zenith);
        Scalar(state,"地平線の高さ###Horizon height",sky.horizonHeight,0.001f,2);
        Color(state,"太陽の色###Sun color",sky.sunColor);
        Vector2(state,"太陽の中心###Sun center",sky.sunCenter,-10,10); Vector2(state,"太陽の半径###Sun radius",sky.sunRadius,0.001f,10);
        Scalar(state,"太陽の強さ###Sun strength",sky.sunStrength,0,1);
        Color(state,"雲の下部の色###Cloud bottom color",sky.cloudLow); Color(state,"雲の上部の色###Cloud top color",sky.cloudHigh);
        Scalar(state,"雲の不透明度###Cloud opacity",sky.cloudOpacity,0,1);
        Vector2(state,"雲の速度（UV/秒）###Cloud velocity (UV/s)",sky.cloudVelocity,-10,10);
        Scalar(state,"空の基準アスペクト比###Sky reference aspect",sky.referenceAspect,0.01f,100);
        for (size_t index=0;index<sky.clouds.size();++index)
        {
            ImGui::PushID(static_cast<int>(index));
            ImGui::Text("雲のまとまり %zu",index+1);
            Vector2(state,"中心（UV）###Center (UV)",sky.clouds[index].center,-10,10);
            Scalar(state,"サイズ###Size",sky.clouds[index].size,0,10);
            ImGui::PopID();
        }
        ImGui::TextWrapped("最初の有効な空が背景になります。画面上の空はトランスフォームでは移動しません。");
    }
    void Emitter(Editor::EditState& state, SceneRuntime::ParticleEmitterComponent& emitter)
    {
        int count=static_cast<int>(emitter.count);
        ImGui::DragInt("パーティクル数###Particle count",&count,1,0,1024,"%d",ImGuiSliderFlags_AlwaysClamp); Track(state);
        emitter.count=static_cast<unsigned int>(count);
        ImGui::ColorEdit4("パーティクルの色・不透明度###Particle color / opacity",emitter.color.data()); Track(state);
        Scalar(state,"サイズ（ワールド単位）###Size (world units)",emitter.size,0.001f,1000);
        Vector(state,"ローカル発生範囲###Local spawn extent",emitter.extent,0,100000);
        Vector(state,"1周期のローカル移動量###Local travel per cycle",emitter.travel,-100000,100000);
        Scalar(state,"周期（秒）###Cycle (seconds)",emitter.cycle,0.01f,100000);
        Scalar(state,"ローカル水平揺らぎ###Local horizontal drift",emitter.drift,0,100000);
        ImGui::TextWrapped("トランスフォームが発生原点を設定します。親の変換で発生範囲も移動し、再生中は発光パーティクルが動きます。");
    }
    void Sway(Editor::EditState& state, SceneRuntime::CameraSwayComponent& sway)
    {
        Vector(state,"ローカル揺れ幅###Local sway amplitude",sway.amplitude,-100000,100000);
        Vector(state,"周期（秒）###Period (seconds)",sway.period,0.01f,100000);
        ImGui::TextWrapped("このオブジェクトにカメラを追加してください。再生中は保存したトランスフォームを変えずに視点を揺らします。");
    }
    template<class T>
    void LocalLight(Editor::EditState& state,T& light)
    {
        Color(state,"光の色###Local light color",light.color);
        Scalar(state,"強度###Local light intensity",light.intensity,0,10000);
        Scalar(state,"到達距離###Local light range",light.range,.01f,100000);
    }
    void Spot(Editor::EditState& state, SceneRuntime::SpotLightComponent& light)
    {
        LocalLight(state,light);
        Scalar(state,"内側の角度（度）###Inner angle",light.innerAngle,0,light.outerAngle-.1f);
        Scalar(state,"外側の角度（度）###Outer angle",light.outerAngle,std::max(1.0f,light.innerAngle+.1f),179);
        ImGui::TextWrapped("トランスフォームのローカル+Z方向へ照射します。角度は円錐全体の開きです。");
    }
    void Light(Editor::EditState& state, SceneRuntime::DirectionalLightComponent& light)
    {
        Vector(state,"ローカル方向###Local direction",light.direction,-100000,100000);
        Color(state,"光の色###Light color",light.color);
        Scalar(state,"強度###Intensity",light.intensity,0,100);
        Scalar(state,"環境光の強度###Ambient intensity",light.ambient,0,100);
        Scalar(state,"鏡面反射の強さ###Specular strength",light.specular,0,100);
        Scalar(state,"光沢###Shininess",light.shininess,1,10000);
        ImGui::Checkbox("リアルタイムの影###Realtime shadows",&light.shadowsEnabled); Track(state);
        Scalar(state,"影の描画範囲###Shadow distance",light.shadowDistance,10,200);
        ImGui::DragFloat("影の深度バイアス###Shadow bias",&light.shadowBias,.00001f,0,.01f,"%.5f",ImGuiSliderFlags_AlwaysClamp); Track(state);
        ImGui::TextWrapped("ヒエラルキーで最初の有効な平行光源がシーンを照らします。");
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
        Component(state,placement.camera,"カメラ###Camera",[&](auto& value) { Camera(state,value); });
        Component(state,placement.directionalLight,"平行光源###DirectionalLight",[&](auto& value) { Light(state,value); });
        Component(state,placement.pointLight,"点光源###PointLight",[&](auto& value) { LocalLight(state,value); });
        Component(state,placement.spotLight,"スポットライト###SpotLight",[&](auto& value) { Spot(state,value); });
        Component(state,placement.sky,"空###Sky",[&](auto& value) { Sky(state,value); });
        Component(state,placement.particleEmitter,"パーティクル発生源###ParticleEmitter",[&](auto& value) { Emitter(state,value); });
        Component(state,placement.cameraSway,"カメラの揺れ###CameraSway",[&](auto& value) { Sway(state,value); });
        return !placement.SameComponents(before);
    }
    bool EnvironmentPanel::Add(SceneRuntime::ScenePlacement& placement)
    {
        bool changed=false;
        if (ImGui::MenuItem("カメラ###Camera",nullptr,false,!placement.camera))
        { const auto id=NewId(placement,"camera"); placement.camera.emplace(); placement.camera->id=id; changed=true; }
        if (ImGui::MenuItem("平行光源###DirectionalLight",nullptr,false,!placement.directionalLight))
        { const auto id=NewId(placement,"light"); placement.directionalLight.emplace(); placement.directionalLight->id=id; changed=true; }
        if (ImGui::MenuItem("点光源###PointLight",nullptr,false,!placement.pointLight))
        { const auto id=NewId(placement,"pointLight"); placement.pointLight.emplace(); placement.pointLight->id=id; changed=true; }
        if (ImGui::MenuItem("スポットライト###SpotLight",nullptr,false,!placement.spotLight))
        { const auto id=NewId(placement,"spotLight"); placement.spotLight.emplace(); placement.spotLight->id=id; changed=true; }
        if (ImGui::MenuItem("空###Sky",nullptr,false,!placement.sky))
        { const auto id=NewId(placement,"sky"); placement.sky.emplace(); placement.sky->id=id; changed=true; }
        if (ImGui::MenuItem("パーティクル発生源###ParticleEmitter",nullptr,false,!placement.particleEmitter))
        { const auto id=NewId(placement,"particles"); placement.particleEmitter.emplace(); placement.particleEmitter->id=id; changed=true; }
        if (ImGui::MenuItem("カメラの揺れ###CameraSway",nullptr,false,!placement.cameraSway))
        { const auto id=NewId(placement,"sway"); placement.cameraSway.emplace(); placement.cameraSway->id=id; changed=true; }
        return changed;
    }
}
