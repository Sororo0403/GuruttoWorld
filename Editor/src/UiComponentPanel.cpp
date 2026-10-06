#include "UiComponentPanel.h"
#include "EnvironmentPanel.h"
#include "AudioPreview.h"
#include <imgui.h>
#include <algorithm>
namespace {
void Track(Editor::EditState& state) {if(ImGui::IsItemActive()||ImGui::IsItemDeactivatedAfterEdit()) state.SetInteraction("component/"+std::to_string(ImGui::GetItemID()));}
void String(Editor::EditState& state,const char* label,std::string& value) {
    std::vector<char> data(std::max(size_t(16385),value.size()+1)); std::copy(value.begin(),value.end(),data.begin());
    if(ImGui::InputText(label,data.data(),data.size())) value=data.data(); Track(state);
}
void Vector(Editor::EditState& state,const char* label,std::array<float,2>& value,float low,float high) {
    ImGui::DragFloat2(label,value.data(),1,low,high,"%.2f",ImGuiSliderFlags_AlwaysClamp); Track(state);
}
void Color(Editor::EditState& state,const char* label,std::array<float,4>& value) {ImGui::ColorEdit4(label,value.data()); Track(state);}
void Asset(std::filesystem::path& value,const Editor::ProjectCatalog* catalog,Editor::AssetKind kind) {
    if(!catalog) return;
    if(ImGui::BeginCombo("Asset",Editor::ProjectCatalog::Text(value).c_str())) {
        if(ImGui::Selectable("None",value.empty())) value.clear();
        for(const auto& a:catalog->Assets()) if(a.kind==kind && ImGui::Selectable(Editor::ProjectCatalog::Text(a.path).c_str(),a.path==value)) value=a.path;
        ImGui::EndCombo();
    }
}
template<class T,class Draw> void Component(std::optional<T>& c,const char* name,Draw draw) {
    if(!c || !ImGui::CollapsingHeader(name,ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::PushID(c->id.c_str()); ImGui::Checkbox("Enabled",&c->enabled); draw(*c);
    if(ImGui::Button("Reset")) {const auto id=c->id; c=T{}; c->id=id;}
    ImGui::SameLine(); if(ImGui::Button("Remove")) c.reset(); ImGui::PopID();
}
void Rect(Editor::EditState& state,SceneRuntime::RectTransformComponent& c) {
    Vector(state,"Anchor min",c.anchorMin,0,1); Vector(state,"Anchor max",c.anchorMax,0,1);
    for(size_t i=0;i<2;++i) c.anchorMax[i]=std::max(c.anchorMax[i],c.anchorMin[i]);
    Vector(state,"Pivot",c.pivot,0,1); Vector(state,"UI position",c.position,-100000,100000); Vector(state,"UI size",c.size,0,100000);
    ImGui::DragFloat("UI rotation (radians)",&c.rotation,0.01f); Track(state);
    String(state,"Visible when (key=value&...)",c.visibleWhen); String(state,"X offset binding",c.offsetBinding); String(state,"Opacity binding",c.opacityBinding); String(state,"Width binding",c.widthBinding);
    ImGui::DragFloat("Intro delay",&c.introDelay,0.01f,0,0.99f,"%.2f",ImGuiSliderFlags_AlwaysClamp); Track(state);
    ImGui::DragFloat("Intro X offset",&c.introOffset,1); Track(state);
}
}
namespace Editor {
bool UiComponentPanel::Draw(EditState& state,SceneRuntime::ScenePlacement& p,const ProjectCatalog* catalog) {
    const auto before=p;
    Component(p.canvas,"Canvas",[&](auto& c){Vector(state,"Reference resolution",c.referenceSize,1,8192);
        std::string eraseKey;
        for(auto& [key,value]:c.stateDefaults) {ImGui::PushID(key.c_str()); ImGui::DragFloat(key.c_str(),&value,0.1f,-100000,100000,"%.2f",ImGuiSliderFlags_AlwaysClamp); Track(state); ImGui::SameLine(); if(ImGui::SmallButton("X")) eraseKey=key; ImGui::PopID();}
        if(!eraseKey.empty()) c.stateDefaults.erase(eraseKey);
        static std::array<char,128> key{}; ImGui::InputText("New state key",key.data(),key.size());
        if(ImGui::Button("Add state value") && key[0]) c.stateDefaults.try_emplace(key.data(),0.0f);});
    Component(p.rectTransform,"RectTransform",[&](auto& c){Rect(state,c);});
    Component(p.image,"Image",[&](auto& c){Asset(c.texture,catalog,AssetKind::Texture); Color(state,"Image tint",c.color); ImGui::DragFloat4("UV rect",c.uv.data(),0.001f,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp); Track(state); c.uv[2]=std::max(c.uv[0],c.uv[2]); c.uv[3]=std::max(c.uv[1],c.uv[3]);});
    Component(p.text,"Text",[&](auto& c){String(state,"Content",c.text); String(state,"Font family",c.font); ImGui::DragFloat("Font size",&c.fontSize,1,1,512,"%.0f",ImGuiSliderFlags_AlwaysClamp); Track(state); Color(state,"Text color",c.color);});
    Component(p.button,"Button",[&](auto& c){
        if(ImGui::BeginCombo("Action",c.action.c_str())) {for(const auto* a:{"click","show","hide","toggle","playAudio","loadScene","quit","setState"}) if(ImGui::Selectable(a,c.action==a)) c.action=a; ImGui::EndCombo();}
        String(state,"Target / state assignments",c.target); String(state,"Game event",c.event); String(state,"Click audio object",c.sound);
        Color(state,"Hover tint",c.hoverColor); Color(state,"Pressed tint",c.pressedColor);
    });
    Component(p.audioSource,"AudioSource",[&](auto& c){Asset(c.clip,catalog,AssetKind::Audio); ImGui::SliderFloat("Volume",&c.volume,0,1); Track(state); ImGui::Checkbox("Loop",&c.loop); ImGui::Checkbox("Play on awake",&c.playOnAwake); String(state,"Cue",c.cue); String(state,"Volume binding",c.volumeBinding); if(ImGui::Button("Audition")) AudioPreview::Play(c.clip,c.volume,c.loop); ImGui::SameLine(); if(ImGui::Button("Stop audition")) AudioPreview::StopRequest();});
    return !p.SameComponents(before);
}
bool UiComponentPanel::Add(SceneRuntime::ScenePlacement& p) {
    bool edited=false;
    if(ImGui::MenuItem("Canvas",nullptr,false,!p.canvas)) {const auto id=EnvironmentPanel::NewId(p,"canvas"); p.canvas.emplace(); p.canvas->id=id; edited=true;}
    if(ImGui::MenuItem("RectTransform",nullptr,false,!p.rectTransform)) {const auto id=EnvironmentPanel::NewId(p,"rectTransform"); p.rectTransform.emplace(); p.rectTransform->id=id; edited=true;}
    if(ImGui::MenuItem("Image",nullptr,false,!p.image)) {const auto id=EnvironmentPanel::NewId(p,"image"); p.image.emplace(); p.image->id=id; edited=true;}
    if(ImGui::MenuItem("Text",nullptr,false,!p.text)) {const auto id=EnvironmentPanel::NewId(p,"text"); p.text.emplace(); p.text->id=id; edited=true;}
    if(ImGui::MenuItem("Button",nullptr,false,!p.button)) {const auto id=EnvironmentPanel::NewId(p,"button"); p.button.emplace(); p.button->id=id; edited=true;}
    if(ImGui::MenuItem("AudioSource",nullptr,false,!p.audioSource)) {const auto id=EnvironmentPanel::NewId(p,"audioSource"); p.audioSource.emplace(); p.audioSource->id=id; edited=true;}
    return edited;
}
}
