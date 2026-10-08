#pragma once
#include "EditState.h"
#include "ProjectCatalog.h"
#include <SceneRuntime/MenuConfiguration.h>
#include <imgui.h>
#include <algorithm>

namespace Editor {
inline void DrawMenuConfiguration(EditState& state, SceneRuntime::CanvasComponent& canvas,const ProjectCatalog* catalog) {
    bool enabled=canvas.menu.has_value();
    if(ImGui::Checkbox("メニューを使用###Use menu",&enabled)) {
        if(enabled) {
            canvas.menu=SceneRuntime::DefaultMenuConfiguration();
            const std::pair<const char*,float> defaults[]={{"introDuration",.65f},{"startDuration",.32f},{"selectionDuration",.16f},{"selectionOffset",12.0f},{"inactiveOpacity",.6f},{"musicFadeDuration",.4f},{"transitionPinkScale",1.25f},{"screen",0.0f},{"selected",0.0f},{"intro",1.0f},{"row",0.0f},{"homeFocusTime",-1.0f},{"configFocusTime",-1.0f},{"quitFocusTime",-1.0f}};
            for(const auto& [key,value]:defaults) canvas.stateDefaults.try_emplace(key,value);
        } else canvas.menu.reset();
    }
    if(!canvas.menu || !ImGui::TreeNode("メニュー設定###Menu configuration")) return;
    auto& c=*canvas.menu;
    const auto string=[&](const char* label,std::string& value) {
        std::vector<char> data(std::max(size_t(16385),value.size()+1)); std::copy(value.begin(),value.end(),data.begin());
        if(ImGui::InputText(label,data.data(),data.size(),ImGuiInputTextFlags_EnterReturnsTrue)) value=data.data();
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Enterで文字列を反映します");
        if(ImGui::IsItemActive() || ImGui::IsItemDeactivatedAfterEdit()) state.SetInteraction("menu/"+std::to_string(ImGui::GetItemID()));
    };
    const auto number=[&](const char* label,float& value,float low=-100000,float high=100000) {
        ImGui::DragFloat(label,&value,.01f,low,high,"%.3f",ImGuiSliderFlags_AlwaysClamp);
        if(ImGui::IsItemActive() || ImGui::IsItemDeactivatedAfterEdit()) state.SetInteraction("menu/"+std::to_string(ImGui::GetItemID()));
    };
    const auto choice=[](const char* label,std::string& value,std::initializer_list<const char*> options) {
        if(ImGui::BeginCombo(label,value.c_str())) {for(const auto* option:options) if(ImGui::Selectable(option,value==option)) value=option; ImGui::EndCombo();}
    };
    ImGui::Checkbox("任意の入力で開始###Press any input",&c.pressAnyButton);
    string("カメラ焦点の状態キー###Focus state",c.focusState);
    if(ImGui::TreeNode("項目・遷移先###Menu entries")) {
        ImGui::TextWrapped("Button のゲームイベントを menu:番号 に設定します。番号は項目を識別し、Button の配置順で上下移動します。");
        int remove=-1;
        for(size_t i=0;i<c.entries.size();++i) {ImGui::PushID(static_cast<int>(i)); auto& e=c.entries[i];
            ImGui::Separator(); ImGui::InputInt("番号###Index",&e.index); e.index=std::clamp(e.index,0,127);
            choice("動作###Operation",e.action,{"loadScene","settings","quit","setState"});
            if(e.action=="loadScene" && catalog && ImGui::BeginCombo("遷移先シーン###Scene target",e.target.empty()?"未設定":e.target.c_str())) {
                for(const auto& asset:catalog->Assets()) if(asset.kind==AssetKind::Scene) {const auto path=ProjectCatalog::Text(asset.path); if(ImGui::Selectable(path.c_str(),path==e.target)) e.target=path;}
                ImGui::EndCombo();
            }
            if(e.action=="setState") string("状態値の代入###Assignments",e.target);
            number("カメラの焦点値###Focus",e.focus); number("焦点ビュー値###View",e.view);
            string("カメラ移動Animationの時計###Focus animation clock",e.focusClock);
            if(ImGui::SmallButton("項目を削除###Remove entry")) remove=static_cast<int>(i); ImGui::PopID();
        }
        if(remove>=0) c.entries.erase(c.entries.begin()+remove);
        if(c.entries.size()<128 && ImGui::Button("項目を追加###Add entry")) {
            int index=0; while(std::any_of(c.entries.begin(),c.entries.end(),[&](const auto& e){return e.index==index;})) ++index;
            c.entries.push_back({index,"loadScene","",static_cast<float>(index),1});
        }
        ImGui::TreePop();
    }
    if(ImGui::TreeNode("設定画面の項目###Settings rows")) {
        ImGui::TextWrapped("Button のイベント settings:行番号 で編集します。独自の設定キーも保存できます。volume は音量、motion は背景演出に使用します。");
        int remove=-1;
        for(size_t i=0;i<c.settings.size();++i) {ImGui::PushID(static_cast<int>(i)); auto& row=c.settings[i];
            ImGui::Separator(); ImGui::Text("行 %d",static_cast<int>(i));
            choice("編集方法###Kind",row.action,{"number","toggle","save"});
            if(row.action!="save") {string("保存キー###Setting key",row.key); number("最小値###Minimum",row.minimum); number("最大値###Maximum",row.maximum);
                row.maximum=std::max(row.maximum,row.minimum+.01f); number("刻み###Step",row.step,.001f,100000);
                number("初期値###Initial",row.initial,row.minimum,row.maximum); row.initial=std::clamp(row.initial,row.minimum,row.maximum);}
            if(ImGui::SmallButton("行を削除###Remove row")) remove=static_cast<int>(i); ImGui::PopID();
        }
        if(remove>=0) c.settings.erase(c.settings.begin()+remove);
        if(c.settings.size()<128 && ImGui::Button("設定項目を追加###Add setting")) {
            int index=0; std::string key; do {key="setting"+std::to_string(index++);} while(std::any_of(c.settings.begin(),c.settings.end(),[&](const auto& s){return s.key==key;}));
            c.settings.push_back({key,"number",0,10,1,0});
        }
        ImGui::TreePop();
    }
    if(ImGui::TreeNode("入力Action・操作音###Inputs and audio cues")) {
        const char* labels[]{"上###Up","下###Down","左###Left","右###Right","決定###Confirm","取消###Cancel"};
        for(size_t i=0;i<c.inputs.size();++i) string(labels[i],c.inputs[i]);
        const char* cues[]{"選択時のキュー###Select cue","決定時のキュー###Confirm cue","取消時のキュー###Back cue","保存失敗時のキュー###Error cue"};
        for(size_t i=0;i<c.cues.size();++i) string(cues[i],c.cues[i]);
        ImGui::TreePop();
    }
    if(ImGui::TreeNode("状態値の出力###State outputs")) {
        ImGui::TextWrapped("入力値: selected / row / screen / intro / selectionPulse / transition / cameraFocus / focusView / quitTransition / volume / motion / setting:保存キー。倍率キーはCanvasの状態値を参照します。出力をUI・Animationの状態キーへ割り当てます。");
        int remove=-1;
        for(size_t i=0;i<c.bindings.size();++i) {ImGui::PushID(static_cast<int>(i)); auto& b=c.bindings[i];
            if(ImGui::TreeNode("Binding", "%s",b.key.c_str())) {
                string("出力キー###Output key",b.key); string("入力値###Source",b.source);
                choice("計算###Calculation",b.operation,{"value","equal","notEqual","emphasis"});
                number("比較値###Compare",b.compare); string("倍率・非選択値のキー###Factor key",b.factor);
                number("倍率キーがない場合の値###Factor fallback",b.factorDefault);
                number("倍率###Scale",b.scale); number("加算###Offset",b.offset); number("出力最小###Clamp minimum",b.minimum); number("出力最大###Clamp maximum",b.maximum);
                b.maximum=std::max(b.minimum,b.maximum);
                if(ImGui::SmallButton("出力を削除###Remove binding")) remove=static_cast<int>(i); ImGui::TreePop();
            } ImGui::PopID();
        }
        if(remove>=0) c.bindings.erase(c.bindings.begin()+remove);
        if(c.bindings.size()<256 && ImGui::Button("状態出力を追加###Add output")) {
            int index=0; std::string key; do {key="output"+std::to_string(index++);} while(std::any_of(c.bindings.begin(),c.bindings.end(),[&](const auto& b){return b.key==key;}));
            c.bindings.push_back({key,"selected"});
        }
        ImGui::TreePop();
    }
    try {SceneRuntime::ValidateMenu(c);} catch(const std::exception& e) {ImGui::TextWrapped("保存前に修正してください: %s",e.what());}
    ImGui::TreePop();
}
}
