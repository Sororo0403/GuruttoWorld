#include "ComponentWidgets.h"
#include "EnvironmentPanel.h"
#include <imgui.h>

namespace
{
    void Track(Editor::EditState& state)
    {
        Editor::TrackComponentInteraction(state);
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
        bool ocean=sky.ocean.has_value();
        if (ImGui::Checkbox("立体的な海と夕空###Ocean environment",&ocean))
        { if (ocean) sky.ocean.emplace(); else sky.ocean.reset(); }
        Track(state);
        if (sky.ocean)
        {
            auto& settings=*sky.ocean;
            Scalar(state,"太陽の高度（度）###Solar elevation",settings.sunElevation,-6,85);
            Scalar(state,"太陽の方位（北0・東90度）###Solar azimuth",settings.sunAzimuth,0,360);
            Scalar(state,"太陽の視直径（度）###Solar diameter",settings.sunDiameter,.1f,2);
            Scalar(state,"水の屈折率###Water refractive index",settings.refractiveIndex,1.01f,2);
            Scalar(state,"海面の高さ（m）###Sea level",settings.seaLevel,-10000,10000);
            Scalar(state,"波の振幅（m）###Wave amplitude",settings.waveAmplitude,0,1);
            Scalar(state,"波の周期（秒）###Wave period",settings.wavePeriod,1,20);
            Scalar(state,"雲量###Ocean cloud coverage",settings.cloudCoverage,0,1);
            Scalar(state,"雲の移動速度（m/秒）###Cloud speed",settings.cloudSpeed,0,30);
            Scalar(state,"エアロゾル密度倍率###Atmospheric haze",settings.haze,.1f,5);
            Scalar(state,"雲底高度（m）###Cloud base",settings.cloudBase,100,12000);
            Scalar(state,"雲層の厚さ（m）###Cloud thickness",settings.cloudThickness,100,6000);
            ImGui::DragFloat("雲の消散係数（1/m）###Cloud extinction",&settings.cloudExtinction,.0001f,.0001f,.03f,"%.4f",ImGuiSliderFlags_AlwaysClamp); Track(state);
            Scalar(state,"オゾン密度倍率###Ozone density",settings.ozoneStrength,0,3);
            Scalar(state,"雲の散乱異方性###Cloud anisotropy",settings.cloudAnisotropy,0,.9f);
            if(ImGui::TreeNode("描画品質と形状###Ocean quality and shape")) {
                int atmosphereSteps=static_cast<int>(settings.atmosphereSteps);if(ImGui::SliderInt("大気の視線サンプル数###atmosphereSteps",&atmosphereSteps,1,64)) settings.atmosphereSteps=static_cast<unsigned int>(atmosphereSteps);Track(state);
                int solarSteps=static_cast<int>(settings.solarSteps);if(ImGui::SliderInt("大気の太陽光サンプル数###solarSteps",&solarSteps,1,32)) settings.solarSteps=static_cast<unsigned int>(solarSteps);Track(state);
                int cloudSteps=static_cast<int>(settings.cloudSteps);if(ImGui::SliderInt("雲の視線サンプル数###cloudSteps",&cloudSteps,1,96)) settings.cloudSteps=static_cast<unsigned int>(cloudSteps);Track(state);
                int cloudLightSteps=static_cast<int>(settings.cloudLightSteps);if(ImGui::SliderInt("雲の太陽光サンプル数###cloudLightSteps",&cloudLightSteps,1,32)) settings.cloudLightSteps=static_cast<unsigned int>(cloudLightSteps);Track(state);
                int waveCount=static_cast<int>(settings.waveCount);if(ImGui::SliderInt("波の成分数###waveCount",&waveCount,1,16)) settings.waveCount=static_cast<unsigned int>(waveCount);Track(state);
                int seaSteps=static_cast<int>(settings.seaSteps);if(ImGui::SliderInt("海面探索回数###seaSteps",&seaSteps,1,64)) settings.seaSteps=static_cast<unsigned int>(seaSteps);Track(state);
                int seaRefineSteps=static_cast<int>(settings.seaRefineSteps);if(ImGui::SliderInt("海面補間回数###seaRefineSteps",&seaRefineSteps,1,32)) settings.seaRefineSteps=static_cast<unsigned int>(seaRefineSteps);Track(state);
                Scalar(state,"太陽光の強度###solarIntensity",settings.solarIntensity,0.0f,1000.0f);
                Scalar(state,"波の基準方向（ラジアン）###waveDirection",settings.waveDirection,-6.284f,6.284f);
                Scalar(state,"波の方向間隔（ラジアン）###waveSpread",settings.waveSpread,-6.284f,6.284f);
                Scalar(state,"波の周期比###wavePeriodRatio",settings.wavePeriodRatio,0.2f,0.99f);
                Scalar(state,"波の振幅比###waveAmplitudeRatio",settings.waveAmplitudeRatio,0.01f,0.95f);
                Scalar(state,"波の位相間隔###wavePhase",settings.wavePhase,0.0f,20.0f);
                Scalar(state,"雲ノイズの空間倍率###cloudScale",settings.cloudScale,1e-05f,0.1f);
                Scalar(state,"海面の最大描画距離###traceDistance",settings.traceDistance,1000.0f,1000000.0f);
                Scalar(state,"雲の最大描画距離###cloudDistance",settings.cloudDistance,1000.0f,1000000.0f);
                ImGui::DragFloat3("太陽光の色###solarColor",settings.solarColor.data(),.0001f,0.0f,1.0f,"%.5f",ImGuiSliderFlags_AlwaysClamp);Track(state);
                ImGui::DragFloat2("雲の移動方向倍率###cloudWind",settings.cloudWind.data(),.0001f,-10.0f,10.0f,"%.5f",ImGuiSliderFlags_AlwaysClamp);Track(state);
                ImGui::DragFloat3("雲ノイズの混合比###cloudNoiseWeights",settings.cloudNoiseWeights.data(),.0001f,0.0f,1.0f,"%.5f",ImGuiSliderFlags_AlwaysClamp);Track(state);
                ImGui::DragFloat3("雲ノイズの周波数比###cloudNoiseFrequencies",settings.cloudNoiseFrequencies.data(),.0001f,0.01f,100.0f,"%.5f",ImGuiSliderFlags_AlwaysClamp);Track(state);
                ImGui::DragFloat3("雲ノイズの位相###cloudNoiseOffsets",settings.cloudNoiseOffsets.data(),.0001f,-1000.0f,1000.0f,"%.5f",ImGuiSliderFlags_AlwaysClamp);Track(state);
                ImGui::TreePop();
            }
            Color(state,"水中の色###Water body color",settings.waterColor);
            Scalar(state,"雲の不透明度###Ocean cloud opacity",sky.cloudOpacity,0,1);
            ImGui::TextWrapped("カメラからのレイで波面・球状大気・立体的な雲を計算します。海面深度で通常のモデルと前後関係を判定します。+Xが東、+Zが北、1単位が1mです。海面の衝突判定はありません。");
            return;
        }
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
        ImGui::Checkbox("局所光源のリアルタイム影###Local shadows",&light.shadowsEnabled); Track(state);
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
