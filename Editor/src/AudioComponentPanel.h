#pragma once
#include "EnvironmentPanel.h"
#include "AudioPreview.h"
#include <imgui.h>
#include <cmath>
namespace Editor::AudioPanel {
/// <summary>音響フィールドの操作をUndoへまとめます。</summary>
inline void Track(EditState& state) {if(ImGui::IsItemActive()||ImGui::IsItemDeactivatedAfterEdit()) state.SetInteraction("component/"+std::to_string(ImGui::GetItemID()));}
/// <summary>音響名・状態キーを編集します。</summary>
inline void String(EditState& state,const char* label,std::string& value) {
    std::array<char,129> data{}; std::copy_n(value.begin(),std::min(value.size(),data.size()-1),data.begin());
    if(ImGui::InputText(label,data.data(),data.size())) value=data.data(); Track(state);
}
/// <summary>音響コンポーネント共通の有効・リセット・削除操作を描画します。</summary>
template<class T,class Draw> inline void Component(std::optional<T>& component,const char* label,Draw draw) {
    if(!component||!ImGui::CollapsingHeader(label,ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::PushID(component->id.c_str()); ImGui::Checkbox("有効###Enabled",&component->enabled); draw(*component);
    if(ImGui::Button("リセット###Reset")) {const auto id=component->id; component=T{}; component->id=id;}
    ImGui::SameLine(); if(ImGui::Button("削除###Remove")) component.reset(); ImGui::PopID();
}
/// <summary>3D音源のクリップ・再生方式・ミキサー・減衰を編集します。</summary>
inline void Source(EditState& state,SceneRuntime::AudioSourceComponent& c,const ProjectCatalog* catalog) {
    if(catalog&&ImGui::BeginCombo("アセット###Asset",ProjectCatalog::Text(c.clip).c_str())) {
        if(ImGui::Selectable("なし###None",c.clip.empty())) c.clip.clear();
        for(const auto& a:catalog->Assets()) if(a.kind==AssetKind::Audio&&ImGui::Selectable(ProjectCatalog::Text(a.path).c_str(),a.path==c.clip)) c.clip=a.path;
        ImGui::EndCombo();
    }
    ImGui::SliderFloat("音量###Volume",&c.volume,0,1); Track(state);
    ImGui::Checkbox("ループ###Loop",&c.loop); ImGui::Checkbox("開始時に再生###Play on awake",&c.playOnAwake);
    ImGui::Checkbox("逐次ストリーミング###Streaming",&c.streaming);
    String(state,"再生キュー名###Cue",c.cue); String(state,"音量の状態キー###Volume binding",c.volumeBinding); String(state,"ミキサーバス###Mixer bus",c.bus);
    if(c.bus.empty()) c.bus="Master";
    ImGui::Checkbox("3D音響###Spatial audio",&c.spatial);
    ImGui::SliderFloat("3D混合率###Spatial blend",&c.spatialBlend,0,1); Track(state);
    ImGui::DragFloat("減衰開始距離###Minimum distance",&c.minimumDistance,.1f,.001f,999999,"%.2f",ImGuiSliderFlags_AlwaysClamp); Track(state);
    ImGui::DragFloat("無音となる距離###Maximum distance",&c.maximumDistance,.1f,std::nextafter(c.minimumDistance,1000000.0f),1000000,"%.2f",ImGuiSliderFlags_AlwaysClamp); Track(state);
    c.maximumDistance=std::max(c.maximumDistance,std::nextafter(c.minimumDistance,1000000.0f));
    ImGui::SliderFloat("ピッチ###Pitch",&c.pitch,.25f,2); Track(state);
    ImGui::SliderFloat("低域フィルタ###Low pass",&c.lowPass,0,1); Track(state);
    if(ImGui::Button("試聴###Audition")) AudioPreview::Play(c.clip,c.volume,c.loop);
    ImGui::SameLine(); if(ImGui::Button("試聴を停止###Stop audition")) AudioPreview::StopRequest();
}
/// <summary>ミキサーグループの実再生パラメーターを編集します。</summary>
inline void Mixer(EditState& state,SceneRuntime::AudioMixerComponent& c) {
    size_t erase=c.groups.size();
    for(size_t i=0;i<c.groups.size();++i) {
        auto& g=c.groups[i]; ImGui::PushID(static_cast<int>(i));
        if(ImGui::TreeNode("Group", "%s",g.name.c_str())) {
            String(state,"バス名###Name",g.name); String(state,"音量の状態キー###Volume binding",g.volumeBinding);
            ImGui::SliderFloat("音量###Volume",&g.volume,0,1); Track(state); ImGui::Checkbox("消音###Mute",&g.mute);
            ImGui::SliderFloat("低域フィルタ###Low pass",&g.lowPass,0,1); Track(state);
            ImGui::SliderFloat("リバーブ混合率###Reverb",&g.reverb,0,1); Track(state);
            if(ImGui::Button("グループを削除###Remove group")) erase=i; ImGui::TreePop();
        } ImGui::PopID();
    }
    if(erase<c.groups.size()) c.groups.erase(c.groups.begin()+static_cast<std::ptrdiff_t>(erase));
    if(c.groups.size()<64&&ImGui::Button("グループを追加###Add group")) {
        std::string name="Bus"; size_t suffix=1;
        while(std::any_of(c.groups.begin(),c.groups.end(),[&](const auto& g){return g.name==name;})) name="Bus"+std::to_string(suffix++);
        SceneRuntime::AudioMixerGroup group; group.name=name; c.groups.push_back(std::move(group));
    }
}
/// <summary>Inspectorの音響コンポーネントを描画します。</summary>
inline void Draw(EditState& state,SceneRuntime::ScenePlacement& p,const ProjectCatalog* catalog) {
    Component(p.audioSource,"音源###AudioSource",[&](auto& c){Source(state,c,catalog);});
    Component(p.audioListener,"音響リスナー###AudioListener",[&](auto& c){ImGui::SliderFloat("全体音量###Volume",&c.volume,0,1); Track(state); ImGui::TextWrapped("有効なリスナーが複数ある場合はHierarchy順の先頭を使用します。未配置時はMain Cameraを使用します。");});
    Component(p.audioMixer,"音響ミキサー###AudioMixer",[&](auto& c){Mixer(state,c);});
}
/// <summary>音響コンポーネント追加メニューを描画します。</summary>
inline bool Add(SceneRuntime::ScenePlacement& p) {
    bool edited=false;
    if(ImGui::MenuItem("音源###AudioSource",nullptr,false,!p.audioSource)) {const auto id=EnvironmentPanel::NewId(p,"audioSource"); p.audioSource.emplace(); p.audioSource->id=id; edited=true;}
    if(ImGui::MenuItem("音響リスナー###AudioListener",nullptr,false,!p.audioListener)) {const auto id=EnvironmentPanel::NewId(p,"audioListener"); p.audioListener.emplace(); p.audioListener->id=id; edited=true;}
    if(ImGui::MenuItem("音響ミキサー###AudioMixer",nullptr,false,!p.audioMixer)) {const auto id=EnvironmentPanel::NewId(p,"audioMixer"); p.audioMixer.emplace(); p.audioMixer->id=id; edited=true;}
    return edited;
}
}
