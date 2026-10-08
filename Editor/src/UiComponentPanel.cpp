#include "UiComponentPanel.h"
#include "EnvironmentPanel.h"
#include "AudioPreview.h"
#include "MenuPanel.h"
#include <imgui.h>
#include <algorithm>
#include <charconv>
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
void TitleSettings(Editor::EditState& state,SceneRuntime::CanvasComponent& canvas) {
    if(!canvas.stateDefaults.contains("introDuration") || !ImGui::TreeNode("タイトル演出の設定###Title presentation settings")) return;
    const auto field=[&](const char* key,const char* label,float fallback,float low,float high) {
        const auto found=canvas.stateDefaults.find(key);
        float value=found==canvas.stateDefaults.end()?fallback:found->second;
        if(ImGui::DragFloat(label,&value,0.01f,low,high,"%.2f",ImGuiSliderFlags_AlwaysClamp)) canvas.stateDefaults[key]=value;
        Track(state);
    };
    field("introDuration","登場・入力待ち時間（秒）###Intro duration",.65f,.01f,10);
    field("startDuration","開始時の遷移時間（秒）###Start duration",.32f,.1f,10);
    field("selectionDuration","選択演出の時間（秒）###Selection duration",.16f,.01f,10);
    field("selectionOffset","選択時の移動量###Selection offset",12,-1000,1000);
    field("inactiveOpacity","非選択項目の不透明度###Inactive opacity",.6f,0,1);
    field("musicFadeDuration","BGMフェード時間（秒）###Music fade duration",.4f,.01f,10);
    field("transitionPinkScale","遷移帯の進行倍率###Transition band scale",1.25f,.01f,10);
    ImGui::TreePop();
}
void Asset(std::filesystem::path& value,const Editor::ProjectCatalog* catalog,Editor::AssetKind kind) {
    if(!catalog) return;
    if(ImGui::BeginCombo("アセット###Asset",Editor::ProjectCatalog::Text(value).c_str())) {
        if(ImGui::Selectable("なし###None",value.empty())) value.clear();
        for(const auto& a:catalog->Assets()) if(a.kind==kind && ImGui::Selectable(Editor::ProjectCatalog::Text(a.path).c_str(),a.path==value)) value=a.path;
        ImGui::EndCombo();
    }
}
template<class T,class Draw> void Component(std::optional<T>& c,const char* name,Draw draw) {
    if(!c || !ImGui::CollapsingHeader(name,ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::PushID(c->id.c_str()); ImGui::Checkbox("有効###Enabled",&c->enabled); draw(*c);
    if(ImGui::Button("リセット###Reset")) {const auto id=c->id; c=T{}; c->id=id;}
    ImGui::SameLine(); if(ImGui::Button("削除###Remove")) c.reset(); ImGui::PopID();
}
void Rect(Editor::EditState& state,SceneRuntime::RectTransformComponent& c) {
    Vector(state,"アンカー最小値###Anchor min",c.anchorMin,0,1); Vector(state,"アンカー最大値###Anchor max",c.anchorMax,0,1);
    for(size_t i=0;i<2;++i) c.anchorMax[i]=std::max(c.anchorMax[i],c.anchorMin[i]);
    Vector(state,"ピボット###Pivot",c.pivot,0,1); Vector(state,"UI位置###UI position",c.position,-100000,100000); Vector(state,"UIサイズ###UI size",c.size,0,100000);
    ImGui::DragFloat("UI回転（ラジアン）###UI rotation (radians)",&c.rotation,0.01f); Track(state);
    String(state,"表示条件（key=value&...）###Visible when (key=value&...)",c.visibleWhen); String(state,"X位置の状態キー###X offset binding",c.offsetBinding); String(state,"不透明度の状態キー###Opacity binding",c.opacityBinding); String(state,"幅の状態キー###Width binding",c.widthBinding);
    ImGui::DragFloat("登場の遅延###Intro delay",&c.introDelay,0.01f,0,0.99f,"%.2f",ImGuiSliderFlags_AlwaysClamp); Track(state);
    ImGui::DragFloat("登場時のX移動量###Intro X offset",&c.introOffset,1); Track(state);
}
}
namespace Editor {
bool UiComponentPanel::Draw(EditState& state,SceneRuntime::ScenePlacement& p,const ProjectCatalog* catalog) {
    const auto before=p;
    Component(p.canvas,"キャンバス###Canvas",[&](auto& c){Vector(state,"基準解像度###Reference resolution",c.referenceSize,1,8192); ImGui::Checkbox("画面サイズに合わせて拡縮###Scale with screen",&c.scaleWithScreen);
        TitleSettings(state,c);
        DrawMenuConfiguration(state,c,catalog);
        std::string eraseKey;
        for(auto& [key,value]:c.stateDefaults) {ImGui::PushID(key.c_str()); ImGui::DragFloat(key.c_str(),&value,0.1f,-100000,100000,"%.2f",ImGuiSliderFlags_AlwaysClamp); Track(state); ImGui::SameLine(); if(ImGui::SmallButton("X")) eraseKey=key; ImGui::PopID();}
        if(!eraseKey.empty()) c.stateDefaults.erase(eraseKey);
        static std::array<char,128> key{}; ImGui::InputText("新しい状態キー###New state key",key.data(),key.size());
        if(ImGui::Button("状態値を追加###Add state value") && key[0]) c.stateDefaults.try_emplace(key.data(),0.0f);});
    Component(p.rectTransform,"UIトランスフォーム###RectTransform",[&](auto& c){Rect(state,c);});
    Component(p.image,"画像###Image",[&](auto& c){Asset(c.texture,catalog,AssetKind::Texture); Color(state,"画像の色###Image tint",c.color); ImGui::DragFloat4("UV範囲###UV rect",c.uv.data(),0.001f,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp); Track(state); c.uv[2]=std::max(c.uv[0],c.uv[2]); c.uv[3]=std::max(c.uv[1],c.uv[3]);});
    Component(p.text,"テキスト###Text",[&](auto& c){String(state,"テキスト内容###Content",c.text); String(state,"フォント名###Font family",c.font); ImGui::DragFloat("文字サイズ###Font size",&c.fontSize,1,1,512,"%.0f",ImGuiSliderFlags_AlwaysClamp); Track(state); Color(state,"文字色###Text color",c.color);});
    Component(p.button,"ボタン###Button",[&](auto& c){
        static constexpr const char* actions[]{"click","show","hide","toggle","playAudio","loadScene","quit","setState"};
        static constexpr const char* labels[]{"クリック通知","表示","非表示","表示切り替え","音声再生","シーン切り替え","実行終了","状態値を設定"};
        const auto found=std::find(std::begin(actions),std::end(actions),c.action);
        const auto preview=found==std::end(actions) ? c.action.c_str() : labels[found-std::begin(actions)];
        if(ImGui::BeginCombo("動作###Action",preview)) {
            for(size_t index=0;index<std::size(actions);++index)
                if(ImGui::Selectable(labels[index],c.action==actions[index])) c.action=actions[index];
            ImGui::EndCombo();
        }
        String(state,"対象・状態値の設定###Target / state assignments",c.target); String(state,"ゲームイベント###Game event",c.event); String(state,"クリック時の音源オブジェクト###Click audio object",c.sound);
        const auto eventType=c.event.starts_with("menu:")?"menu":c.event.starts_with("settings:")?"settings":c.event=="back"?"back":"custom";
        if(ImGui::BeginCombo("メニューイベント###Menu event",eventType)) {
            for(const char* type:{"menu","settings","back","custom"}) if(ImGui::Selectable(type,std::string_view(type)==eventType)) {
                if(std::string_view(type)=="menu" || std::string_view(type)=="settings") {c.event=std::string(type)+":0"; c.action="click"; c.target.clear(); c.sound.clear();}
                else c.event=std::string_view(type)=="back"?"back":"";
            }
            ImGui::EndCombo();
        }
        if(c.event.starts_with("menu:") || c.event.starts_with("settings:")) {
            const auto prefix=c.event.starts_with("menu:")?"menu:":"settings:";
            int index=0; const auto digits=std::string_view(c.event).substr(std::char_traits<char>::length(prefix));
            std::from_chars(digits.data(),digits.data()+digits.size(),index);
            if(ImGui::InputInt("項目番号・設定行###Menu index or row",&index)) c.event=std::string(prefix)+std::to_string(std::clamp(index,0,127));
        }
        if(!c.shortcut.empty()) ImGui::TextWrapped("旧ショートカット: %s（入力Actionへ移行済み）",c.shortcut.c_str());
        String(state,"入力Action名###Input action",c.inputAction);
        Color(state,"ホバー時の色###Hover tint",c.hoverColor); Color(state,"押下時の色###Pressed tint",c.pressedColor);
    });
    Component(p.audioSource,"音源###AudioSource",[&](auto& c){Asset(c.clip,catalog,AssetKind::Audio); ImGui::SliderFloat("音量###Volume",&c.volume,0,1); Track(state); ImGui::Checkbox("ループ###Loop",&c.loop); ImGui::Checkbox("開始時に再生###Play on awake",&c.playOnAwake); String(state,"再生キュー名###Cue",c.cue); String(state,"音量の状態キー###Volume binding",c.volumeBinding); if(ImGui::Button("試聴###Audition")) AudioPreview::Play(c.clip,c.volume,c.loop); ImGui::SameLine(); if(ImGui::Button("試聴を停止###Stop audition")) AudioPreview::StopRequest();});
    return !p.SameComponents(before);
}
bool UiComponentPanel::Add(SceneRuntime::ScenePlacement& p) {
    bool edited=false;
    if(ImGui::MenuItem("キャンバス###Canvas",nullptr,false,!p.canvas)) {const auto id=EnvironmentPanel::NewId(p,"canvas"); p.canvas.emplace(); p.canvas->id=id; edited=true;}
    if(ImGui::MenuItem("UIトランスフォーム###RectTransform",nullptr,false,!p.rectTransform)) {const auto id=EnvironmentPanel::NewId(p,"rectTransform"); p.rectTransform.emplace(); p.rectTransform->id=id; edited=true;}
    if(ImGui::MenuItem("画像###Image",nullptr,false,!p.image)) {const auto id=EnvironmentPanel::NewId(p,"image"); p.image.emplace(); p.image->id=id; edited=true;}
    if(ImGui::MenuItem("テキスト###Text",nullptr,false,!p.text)) {const auto id=EnvironmentPanel::NewId(p,"text"); p.text.emplace(); p.text->id=id; edited=true;}
    if(ImGui::MenuItem("ボタン###Button",nullptr,false,!p.button)) {const auto id=EnvironmentPanel::NewId(p,"button"); p.button.emplace(); p.button->id=id; edited=true;}
    if(ImGui::MenuItem("音源###AudioSource",nullptr,false,!p.audioSource)) {const auto id=EnvironmentPanel::NewId(p,"audioSource"); p.audioSource.emplace(); p.audioSource->id=id; edited=true;}
    return edited;
}
}
