#include <SceneRuntime/Animation.h>
#include <SceneRuntime/SceneLayout.h>
#include <algorithm>
namespace SceneRuntime {
namespace {
bool Set_position(ScenePlacement& p,const std::array<float,4>& v) {if(!(true)) return false;p.position={v[0],v[1],v[2]};return true;}
bool Set_rotation(ScenePlacement& p,const std::array<float,4>& v) {if(!(true)) return false;p.rotation={v[0],v[1],v[2]};return true;}
bool Set_scale(ScenePlacement& p,const std::array<float,4>& v) {if(!(true)) return false;p.scale={v[0],v[1],v[2]};return true;}
bool Set_directionalLight_intensity(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.directionalLight)) return false;p.directionalLight->intensity=v[0];return true;}
bool Set_directionalLight_color(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.directionalLight)) return false;p.directionalLight->color={v[0],v[1],v[2]};return true;}
bool Set_pointLight_intensity(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.pointLight)) return false;p.pointLight->intensity=v[0];return true;}
bool Set_pointLight_color(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.pointLight)) return false;p.pointLight->color={v[0],v[1],v[2]};return true;}
bool Set_spotLight_intensity(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.spotLight)) return false;p.spotLight->intensity=v[0];return true;}
bool Set_spotLight_color(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.spotLight)) return false;p.spotLight->color={v[0],v[1],v[2]};return true;}
bool Set_camera_verticalFov(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.camera)) return false;p.camera->verticalFov=v[0];return true;}
bool Set_camera_nearClip(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.camera)) return false;p.camera->nearClip=v[0];return true;}
bool Set_camera_farClip(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.camera)) return false;p.camera->farClip=v[0];return true;}
bool Set_ocean_sunElevation(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.sky && p.sky->ocean)) return false;p.sky->ocean->sunElevation=v[0];return true;}
bool Set_ocean_sunAzimuth(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.sky && p.sky->ocean)) return false;p.sky->ocean->sunAzimuth=v[0];return true;}
bool Set_ocean_sunDiameter(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.sky && p.sky->ocean)) return false;p.sky->ocean->sunDiameter=v[0];return true;}
bool Set_ocean_waveAmplitude(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.sky && p.sky->ocean)) return false;p.sky->ocean->waveAmplitude=v[0];return true;}
bool Set_ocean_wavePeriod(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.sky && p.sky->ocean)) return false;p.sky->ocean->wavePeriod=v[0];return true;}
bool Set_ocean_seaLevel(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.sky && p.sky->ocean)) return false;p.sky->ocean->seaLevel=v[0];return true;}
bool Set_ocean_cloudCoverage(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.sky && p.sky->ocean)) return false;p.sky->ocean->cloudCoverage=v[0];return true;}
bool Set_ocean_cloudSpeed(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.sky && p.sky->ocean)) return false;p.sky->ocean->cloudSpeed=v[0];return true;}
bool Set_ocean_haze(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.sky && p.sky->ocean)) return false;p.sky->ocean->haze=v[0];return true;}
bool Set_ocean_cloudBase(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.sky && p.sky->ocean)) return false;p.sky->ocean->cloudBase=v[0];return true;}
bool Set_ocean_cloudThickness(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.sky && p.sky->ocean)) return false;p.sky->ocean->cloudThickness=v[0];return true;}
bool Set_ocean_cloudExtinction(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.sky && p.sky->ocean)) return false;p.sky->ocean->cloudExtinction=v[0];return true;}
bool Set_ocean_ozoneStrength(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.sky && p.sky->ocean)) return false;p.sky->ocean->ozoneStrength=v[0];return true;}
bool Set_ocean_cloudAnisotropy(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.sky && p.sky->ocean)) return false;p.sky->ocean->cloudAnisotropy=v[0];return true;}
bool Set_ocean_waterColor(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.sky && p.sky->ocean)) return false;p.sky->ocean->waterColor={v[0],v[1],v[2]};return true;}
bool Set_sky_horizon(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.sky)) return false;p.sky->horizon={v[0],v[1],v[2]};return true;}
bool Set_sky_zenith(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.sky)) return false;p.sky->zenith={v[0],v[1],v[2]};return true;}
bool Set_sky_sunColor(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.sky)) return false;p.sky->sunColor={v[0],v[1],v[2]};return true;}
bool Set_sky_sunStrength(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.sky)) return false;p.sky->sunStrength=v[0];return true;}
bool Set_sky_cloudOpacity(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.sky)) return false;p.sky->cloudOpacity=v[0];return true;}
bool Set_particleEmitter_size(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.particleEmitter)) return false;p.particleEmitter->size=v[0];return true;}
bool Set_particleEmitter_color(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.particleEmitter)) return false;p.particleEmitter->color={v[0],v[1],v[2],v[3]};return true;}
bool Set_material_color(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.material)) return false;p.material->surfaceOverride=true;p.material->color={v[0],v[1],v[2],v[3]};return true;}
bool Set_material_roughness(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.material)) return false;p.material->surfaceOverride=true;p.material->roughness=v[0];return true;}
bool Set_material_metallic(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.material)) return false;p.material->surfaceOverride=true;p.material->metallic=v[0];return true;}
bool Set_ocean_refractiveIndex(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.sky && p.sky->ocean)) return false;p.sky->ocean->refractiveIndex=v[0];return true;}
bool Set_ocean_solarIntensity(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.sky && p.sky->ocean)) return false;p.sky->ocean->solarIntensity=v[0];return true;}
bool Set_ocean_solarColor(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.sky && p.sky->ocean)) return false;p.sky->ocean->solarColor={v[0],v[1],v[2]};return true;}
bool Set_ocean_waveDirection(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.sky && p.sky->ocean)) return false;p.sky->ocean->waveDirection=v[0];return true;}
bool Set_ocean_waveSpread(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.sky && p.sky->ocean)) return false;p.sky->ocean->waveSpread=v[0];return true;}
bool Set_ocean_wavePeriodRatio(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.sky && p.sky->ocean)) return false;p.sky->ocean->wavePeriodRatio=v[0];return true;}
bool Set_ocean_waveAmplitudeRatio(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.sky && p.sky->ocean)) return false;p.sky->ocean->waveAmplitudeRatio=v[0];return true;}
bool Set_ocean_wavePhase(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.sky && p.sky->ocean)) return false;p.sky->ocean->wavePhase=v[0];return true;}
bool Set_ocean_cloudScale(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.sky && p.sky->ocean)) return false;p.sky->ocean->cloudScale=v[0];return true;}
bool Set_ocean_cloudWind(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.sky && p.sky->ocean)) return false;p.sky->ocean->cloudWind={v[0],v[1]};return true;}
bool Set_ocean_cloudNoiseWeights(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.sky && p.sky->ocean)) return false;p.sky->ocean->cloudNoiseWeights={v[0],v[1],v[2]};return true;}
bool Set_ocean_cloudNoiseFrequencies(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.sky && p.sky->ocean)) return false;p.sky->ocean->cloudNoiseFrequencies={v[0],v[1],v[2]};return true;}
bool Set_ocean_cloudNoiseOffsets(ScenePlacement& p,const std::array<float,4>& v) {if(!(p.sky && p.sky->ocean)) return false;p.sky->ocean->cloudNoiseOffsets={v[0],v[1],v[2]};return true;}
}

std::span<const AnimationProperty> Animation::Properties() {
    static const AnimationProperty properties[]{
        {"position","3D位置",-100000.0f,100000.0f,3},
        {"rotation","3D回転",-100000.0f,100000.0f,3},
        {"scale","3Dスケール",-100000.0f,100000.0f,3},
        {"uiPosition","UI位置",-100000.0f,100000.0f,2},
        {"uiSize","UIサイズ",0.0f,100000.0f,2},
        {"uiRotation","UI回転",-100000.0f,100000.0f,1},
        {"opacity","不透明度",0.0f,1.0f,1},
        {"directionalLight.intensity","directionalLight 強度",0.0f,100.0f,1},
        {"directionalLight.color","directionalLight 色",0.0f,1.0f,3},
        {"pointLight.intensity","pointLight 強度",0.0f,10000.0f,1},
        {"pointLight.color","pointLight 色",0.0f,1.0f,3},
        {"spotLight.intensity","spotLight 強度",0.0f,10000.0f,1},
        {"spotLight.color","spotLight 色",0.0f,1.0f,3},
        {"camera.verticalFov","カメラ視野角",1.0f,175.0f,1},
        {"camera.nearClip","カメラ近距離",0.001f,100000.0f,1},
        {"camera.farClip","カメラ遠距離",0.002f,1000000.0f,1},
        {"ocean.sunElevation","太陽高度",-6.0f,85.0f,1},
        {"ocean.sunAzimuth","太陽方位",0.0f,360.0f,1},
        {"ocean.sunDiameter","太陽視直径",0.1f,2.0f,1},
        {"ocean.waveAmplitude","波振幅",0.0f,1.0f,1},
        {"ocean.wavePeriod","波周期",1.0f,20.0f,1},
        {"ocean.seaLevel","海面高度",-10000.0f,10000.0f,1},
        {"ocean.cloudCoverage","雲量",0.0f,1.0f,1},
        {"ocean.cloudSpeed","雲速度",0.0f,30.0f,1},
        {"ocean.haze","霞",0.1f,5.0f,1},
        {"ocean.cloudBase","雲底",100.0f,12000.0f,1},
        {"ocean.cloudThickness","雲厚さ",100.0f,6000.0f,1},
        {"ocean.cloudExtinction","雲消散",0.0001f,0.03f,1},
        {"ocean.ozoneStrength","オゾン",0.0f,3.0f,1},
        {"ocean.cloudAnisotropy","雲散乱",0.0f,0.9f,1},
        {"ocean.waterColor","海の色",0.0f,1.0f,3},
        {"sky.horizon","地平線の色",0.0f,1.0f,3},
        {"sky.zenith","天頂の色",0.0f,1.0f,3},
        {"sky.sunColor","太陽の色",0.0f,1.0f,3},
        {"sky.sunStrength","太陽の強さ",0.0f,1.0f,1},
        {"sky.cloudOpacity","雲不透明度",0.0f,1.0f,1},
        {"particleEmitter.size","粒子サイズ",0.001f,1000.0f,1},
        {"particleEmitter.color","粒子色",0.0f,1.0f,4},
        {"material.color","マテリアル色",0.0f,1.0f,4},
        {"material.roughness","マテリアル粗さ",0.04f,1.0f,1},
        {"material.metallic","マテリアル金属感",0.0f,1.0f,1},
        {"ocean.refractiveIndex","水の屈折率",1.01f,2.0f,1},
        {"ocean.solarIntensity","太陽光強度",0.0f,1000.0f,1},
        {"ocean.solarColor","太陽光色",0.0f,1.0f,3},
        {"ocean.waveDirection","波の基準方向",-6.284f,6.284f,1},
        {"ocean.waveSpread","波の方向間隔",-6.284f,6.284f,1},
        {"ocean.wavePeriodRatio","波の周期比",0.2f,0.99f,1},
        {"ocean.waveAmplitudeRatio","波の振幅比",0.01f,0.95f,1},
        {"ocean.wavePhase","波の位相間隔",0.0f,20.0f,1},
        {"ocean.cloudScale","雲の空間倍率",1e-05f,0.1f,1},
        {"ocean.cloudWind","雲の移動方向倍率",-10.0f,10.0f,2},
        {"ocean.cloudNoiseWeights","雲の混合比",0.0f,1.0f,3},
        {"ocean.cloudNoiseFrequencies","雲の周波数比",0.01f,100.0f,3},
        {"ocean.cloudNoiseOffsets","雲の位相",-1000.0f,1000.0f,3}
    }; return properties;
}
bool Animation::Apply(ScenePlacement& placement,const std::string& property,const std::array<float,4>& value) {
    const auto properties=Properties();
    const auto info=std::find_if(properties.begin(),properties.end(),[&](const auto& p){return property==p.name;});
    if(info==properties.end()) return false;
    auto clamped=value; for(unsigned int i=0;i<info->channels;++i) clamped[i]=std::clamp(clamped[i],info->minimum,info->maximum);
    if(property=="scale") for(unsigned int i=0;i<3;++i) if(std::abs(clamped[i])<.00001f) clamped[i]=std::copysign(.00001f,clamped[i]);
    using Setter=bool (*)(ScenePlacement&,const std::array<float,4>&);
    static const std::map<std::string,Setter> setters{
        {"position",Set_position},
        {"rotation",Set_rotation},
        {"scale",Set_scale},
        {"directionalLight.intensity",Set_directionalLight_intensity},
        {"directionalLight.color",Set_directionalLight_color},
        {"pointLight.intensity",Set_pointLight_intensity},
        {"pointLight.color",Set_pointLight_color},
        {"spotLight.intensity",Set_spotLight_intensity},
        {"spotLight.color",Set_spotLight_color},
        {"camera.verticalFov",Set_camera_verticalFov},
        {"camera.nearClip",Set_camera_nearClip},
        {"camera.farClip",Set_camera_farClip},
        {"ocean.sunElevation",Set_ocean_sunElevation},
        {"ocean.sunAzimuth",Set_ocean_sunAzimuth},
        {"ocean.sunDiameter",Set_ocean_sunDiameter},
        {"ocean.waveAmplitude",Set_ocean_waveAmplitude},
        {"ocean.wavePeriod",Set_ocean_wavePeriod},
        {"ocean.seaLevel",Set_ocean_seaLevel},
        {"ocean.cloudCoverage",Set_ocean_cloudCoverage},
        {"ocean.cloudSpeed",Set_ocean_cloudSpeed},
        {"ocean.haze",Set_ocean_haze},
        {"ocean.cloudBase",Set_ocean_cloudBase},
        {"ocean.cloudThickness",Set_ocean_cloudThickness},
        {"ocean.cloudExtinction",Set_ocean_cloudExtinction},
        {"ocean.ozoneStrength",Set_ocean_ozoneStrength},
        {"ocean.cloudAnisotropy",Set_ocean_cloudAnisotropy},
        {"ocean.waterColor",Set_ocean_waterColor},
        {"sky.horizon",Set_sky_horizon},
        {"sky.zenith",Set_sky_zenith},
        {"sky.sunColor",Set_sky_sunColor},
        {"sky.sunStrength",Set_sky_sunStrength},
        {"sky.cloudOpacity",Set_sky_cloudOpacity},
        {"particleEmitter.size",Set_particleEmitter_size},
        {"particleEmitter.color",Set_particleEmitter_color},
        {"material.color",Set_material_color},
        {"material.roughness",Set_material_roughness},
        {"material.metallic",Set_material_metallic},
        {"ocean.refractiveIndex",Set_ocean_refractiveIndex},
        {"ocean.solarIntensity",Set_ocean_solarIntensity},
        {"ocean.solarColor",Set_ocean_solarColor},
        {"ocean.waveDirection",Set_ocean_waveDirection},
        {"ocean.waveSpread",Set_ocean_waveSpread},
        {"ocean.wavePeriodRatio",Set_ocean_wavePeriodRatio},
        {"ocean.waveAmplitudeRatio",Set_ocean_waveAmplitudeRatio},
        {"ocean.wavePhase",Set_ocean_wavePhase},
        {"ocean.cloudScale",Set_ocean_cloudScale},
        {"ocean.cloudWind",Set_ocean_cloudWind},
        {"ocean.cloudNoiseWeights",Set_ocean_cloudNoiseWeights},
        {"ocean.cloudNoiseFrequencies",Set_ocean_cloudNoiseFrequencies},
        {"ocean.cloudNoiseOffsets",Set_ocean_cloudNoiseOffsets}
    };
    const auto setter=setters.find(property); return setter!=setters.end() && setter->second(placement,clamped);
}
}
