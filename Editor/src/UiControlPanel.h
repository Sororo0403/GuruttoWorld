#pragma once
#include "EditState.h"
#include "EnvironmentPanel.h"
#include <SceneRuntime/SceneLayout.h>
#include <imgui.h>
namespace Editor
{
    /// <summary>汎用UIのプロパティーを通常のComponent編集とUndoへ接続します。</summary>
    inline void DrawUiControls(EditState& state,SceneRuntime::ScenePlacement& p)
    {
        const auto track=[&]() {if(ImGui::IsItemActive() || ImGui::IsItemDeactivatedAfterEdit()) state.SetInteraction("component/"+std::to_string(ImGui::GetItemID()));};
        const auto text=[&](const char* label,std::string& value) {
            std::vector<char> buffer(std::max(size_t(16385),value.size()+1)); std::copy(value.begin(),value.end(),buffer.begin());
            if(ImGui::InputText(label,buffer.data(),buffer.size())) value=buffer.data(); track();
        };
        const auto component=[&](auto& optional,const char* title,auto draw) {
            if(!optional || !ImGui::CollapsingHeader(title,ImGuiTreeNodeFlags_DefaultOpen)) return;
            ImGui::PushID(optional->id.c_str()); ImGui::Checkbox("有効###Enabled",&optional->enabled); draw(*optional);
            if(ImGui::Button("リセット###Reset")) {const auto id=optional->id; optional=std::decay_t<decltype(*optional)>{}; optional->id=id;}
            ImGui::SameLine(); if(ImGui::Button("削除###Remove")) optional.reset(); ImGui::PopID();
        };
        component(p.inputField,"入力欄###InputField",[&](auto& c) {
            text("初期テキスト###Initial text",c.text); text("プレースホルダー###Placeholder",c.placeholder); text("文字列の状態キー###String binding",c.binding);
            int maximum=static_cast<int>(c.maxLength); if(ImGui::InputInt("最大文字数###Maximum characters",&maximum)) c.maxLength=static_cast<unsigned int>(std::clamp(maximum,1,4096)); track();
            ImGui::Checkbox("複数行###Multiline",&c.multiline); ImGui::Checkbox("パスワード###Password",&c.password); ImGui::Checkbox("読み取り専用###Read only",&c.readOnly);
            text("変更イベント###Changed event",c.changedEvent); text("確定イベント###Submitted event",c.submittedEvent);
            ImGui::TextWrapped("Gameビューでクリックして文字入力。Enterで確定、Backspaceで削除。Textコンポーネントで文字色とフォントを設定できます。");
        });
        component(p.slider,"スライダー###Slider",[&](auto& c) {
            ImGui::DragFloat("最小値###Minimum",&c.minimum,.1f,-100000,c.maximum-.001f,"%.3f",ImGuiSliderFlags_AlwaysClamp); track();
            ImGui::DragFloat("最大値###Maximum",&c.maximum,.1f,c.minimum+.001f,100000,"%.3f",ImGuiSliderFlags_AlwaysClamp); track();
            c.value=std::clamp(c.value,c.minimum,c.maximum); ImGui::SliderFloat("初期値###Initial value",&c.value,c.minimum,c.maximum); track();
            ImGui::Checkbox("整数###Whole numbers",&c.wholeNumbers); ImGui::Checkbox("縦方向###Vertical",&c.vertical);
            text("数値の状態キー###Value binding",c.binding); text("変更イベント###Changed event",c.changedEvent); ImGui::ColorEdit4("塗り色###Fill color",c.fillColor.data()); track();
        });
        component(p.toggle,"トグル###Toggle",[&](auto& c) {
            ImGui::Checkbox("初期値###Initial value",&c.value); text("数値の状態キー###Value binding",c.binding); text("変更イベント###Changed event",c.changedEvent);
            ImGui::ColorEdit4("チェック時の色###Checked color",c.checkedColor.data()); track();
        });
        component(p.scrollView,"スクロールビュー###ScrollView",[&](auto& c) {
            ImGui::DragFloat2("コンテンツサイズ###Content size",c.contentSize.data(),1,0,100000,"%.1f",ImGuiSliderFlags_AlwaysClamp); track();
            ImGui::DragFloat2("初期スクロール位置###Initial scroll offset",c.offset.data(),1,0,100000,"%.1f",ImGuiSliderFlags_AlwaysClamp); track();
            ImGui::Checkbox("横スクロール###Horizontal",&c.horizontal); ImGui::Checkbox("縦スクロール###Vertical",&c.vertical);
            ImGui::DragFloat("ホイール速度###Wheel speed",&c.wheelSpeed,1,0,10000,"%.1f",ImGuiSliderFlags_AlwaysClamp); track();
            ImGui::TextWrapped("子オブジェクトをスクロールします。Gameビューのホイールと背景ドラッグで操作できます。");
        });
        component(p.mask,"矩形マスク###Mask",[&](auto&) {ImGui::TextWrapped("子孫の表示とヒット判定をこの矩形内に制限します。");});
        component(p.layoutGroup,"自動レイアウト###LayoutGroup",[&](auto& c) {
            if(ImGui::BeginCombo("方向###Direction",c.direction.c_str())) {for(const char* direction:{"horizontal","vertical","grid"}) if(ImGui::Selectable(direction,c.direction==direction)) c.direction=direction; ImGui::EndCombo();}
            ImGui::DragFloat2("間隔###Spacing",c.spacing.data(),1,0,10000,"%.1f",ImGuiSliderFlags_AlwaysClamp); track();
            ImGui::DragFloat4("余白（左・上・右・下）###Padding",c.padding.data(),1,0,10000,"%.1f",ImGuiSliderFlags_AlwaysClamp); track();
            if(c.direction=="grid") {ImGui::DragFloat2("セルサイズ###Cell size",c.cellSize.data(),1,1,10000,"%.1f",ImGuiSliderFlags_AlwaysClamp); track(); int columns=static_cast<int>(c.columns); if(ImGui::InputInt("列数###Columns",&columns)) c.columns=static_cast<unsigned int>(std::clamp(columns,1,1024)); track();}
            else {ImGui::Checkbox("幅を広げる###Expand width",&c.expandWidth); ImGui::Checkbox("高さを広げる###Expand height",&c.expandHeight);}
            ImGui::TextWrapped("直下のRectTransformをHierarchy順で配置します。個別アンカーと位置は実行時にレイアウトで上書きされます。");
        });
    }
    /// <summary>汎用UIを追加し、必要なRectTransformも作成します。</summary>
    inline bool AddUiControls(SceneRuntime::ScenePlacement& p)
    {
        bool edited=false;
        const auto add=[&](auto& component,const char* label,const char* key) {
            if(ImGui::MenuItem(label,nullptr,false,!component)) {
                const auto id=EnvironmentPanel::NewId(p,key); component.emplace(); component->id=id;
                if(!p.rectTransform) {p.rectTransform.emplace(); p.rectTransform->id=EnvironmentPanel::NewId(p,"rectTransform");}
                edited=true;
            }
        };
        add(p.inputField,"入力欄###InputField","inputField"); add(p.slider,"スライダー###Slider","slider"); add(p.toggle,"トグル###Toggle","toggle");
        add(p.scrollView,"スクロールビュー###ScrollView","scrollView"); add(p.mask,"矩形マスク###Mask","mask"); add(p.layoutGroup,"自動レイアウト###LayoutGroup","layoutGroup");
        return edited;
    }
}
