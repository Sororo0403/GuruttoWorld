#pragma once
#include "ComponentWidgets.h"
#include "EditState.h"
#include "ProjectCatalog.h"
#include <imgui.h>
namespace Editor::SceneSettingsPanel {
inline void Track(EditState& state) {
    Editor::TrackComponentInteraction(state);
}
inline bool Inputs(EditState& state,SceneRuntime::SceneSettings& settings) {
    bool edited=false;
    const char* labels[]{"右移動Action###Player right","左移動Action###Player left","前移動Action###Player forward","後移動Action###Player back","ジャンプAction###Player jump"};
    for(size_t i=0;i<settings.playerInputs.size();++i) {
        std::array<char,129> buffer{};std::copy_n(settings.playerInputs[i].data(),std::min(settings.playerInputs[i].size(),buffer.size()-1),buffer.data());
        if(ImGui::InputText(labels[i],buffer.data(),buffer.size())) {settings.playerInputs[i]=buffer.data();edited=true;}
        Track(state);
    }
    return edited;
}
inline bool Simulation(EditState& state,SceneRuntime::SceneSettings& settings) {
    bool edited=ImGui::DragFloat3("共通重力###World gravity",settings.gravity.data(),.1f,-1000,1000,"%.3f",ImGuiSliderFlags_AlwaysClamp);Track(state);
    edited |= ImGui::DragFloat("物理更新間隔（秒）###Fixed timestep",&settings.fixedStep,.0001f,1.0f/240.0f,.1f,"%.6f",ImGuiSliderFlags_AlwaysClamp);Track(state);
    int steps=static_cast<int>(settings.maximumFixedSteps);
    if(ImGui::SliderInt("1フレームの最大物理更新数###Maximum fixed steps",&steps,1,32)) {settings.maximumFixedSteps=static_cast<unsigned int>(steps);edited=true;}Track(state);
    return edited;
}
inline bool Resolution(const char* label,unsigned int& value,unsigned int high) {
    bool edited=false;
    if(ImGui::BeginCombo(label,std::to_string(value).c_str())) {
        for(unsigned int size=128;size<=high;size*=2) if(ImGui::Selectable(std::to_string(size).c_str(),size==value)) {value=size;edited=true;}
        ImGui::EndCombo();
    }
    return edited;
}
inline bool Shader(const char* label,std::filesystem::path& value,const ProjectCatalog& catalog) {
    bool edited=false;
    if(ImGui::BeginCombo(label,value.empty()?"標準":ProjectCatalog::Text(value).c_str())) {
        if(ImGui::Selectable("標準###Default",value.empty())) {value.clear();edited=true;}
        for(const auto& asset:catalog.Assets()) if(asset.kind==AssetKind::Shader && asset.path.extension()==".hlsl")
            if(ImGui::Selectable(ProjectCatalog::Text(asset.path).c_str(),value==asset.path)) {value=asset.path;edited=true;}
        ImGui::EndCombo();
    }
    return edited;
}
inline bool Rendering(EditState& state,SceneRuntime::SceneSettings& settings,const ProjectCatalog* catalog) {
    bool edited=Resolution("平行光の影解像度###Directional shadow resolution",settings.shadowResolution,4096);
    edited |= Resolution("局所光の影解像度###Local shadow resolution",settings.localShadowResolution,2048);
    edited |= ImGui::SliderFloat("Scene描画倍率###Scene render scale",&settings.sceneRenderScale,.25f,1);Track(state);
    edited |= ImGui::SliderFloat("Game描画倍率###Game render scale",&settings.gameRenderScale,.25f,1);Track(state);
    edited |= ImGui::Checkbox("Sceneを毎フレーム描画###Scene realtime",&settings.sceneRealtime);
    edited |= ImGui::Checkbox("Gameを毎フレーム描画###Game realtime",&settings.gameRealtime);
    if(catalog) {
        edited |= Shader("空のシェーダー###Sky shader",settings.skyShader,*catalog);
        edited |= Shader("海のシェーダー###Ocean shader",settings.oceanShader,*catalog);
        edited |= Shader("粒子のシェーダー###Particle shader",settings.particleShader,*catalog);
        edited |= Shader("ポストエフェクトのシェーダー###Post shader",settings.postShader,*catalog);
    }
    return edited;
}
}
