#pragma once
#include "UiCanvasPanel.h"
#include "UiLayoutTools.h"
#include <imgui.h>

namespace Editor
{
    class UiEditorPanel final
    {
    public:
        bool Visible() const { return visible_; }
        const SceneRuntime::UiState& PreviewState() const { return preview_; }
        std::array<unsigned int,2> Resolution() const { return resolution_; }
        void Draw(std::uint64_t texture,const SceneRuntime::SceneLayout& layout,EditState& state,bool enabled)
        {
            visible_=false;
            if (!ImGui::Begin("UI編集###UI Editor")) { ImGui::End(); return; }
            visible_=true;
            ImGui::SetNextItemWidth(130); ImGui::SliderFloat("表示倍率###UI zoom",&zoom_,.25f,4,"%.2fx");
            ImGui::SameLine(); if (ImGui::Button("表示を戻す###Reset UI view")) {zoom_=1; resetScroll_=true;}
            ImGui::SameLine(); ImGui::Checkbox("スナップ###UI snap",&snap_);
            ImGui::SameLine(); ImGui::SetNextItemWidth(70); ImGui::DragFloat("間隔###UI grid",&grid_,1,1,1000,"%.0f",ImGuiSliderFlags_AlwaysClamp);
            auto defaults=SceneRuntime::SceneUi::Defaults(layout);
            if (defaults.values.contains("screen")) {
                float screen=preview_.Value("screen",defaults.Value("screen"));
                int selected=static_cast<int>(screen); ImGui::SetNextItemWidth(150);
                if (ImGui::Combo("画面プレビュー###UI screen preview",&selected,"メイン\0設定\0入力待ち\0")) overrides_["screen"]=static_cast<float>(selected);
            }
            if (ImGui::TreeNode("状態値のプレビュー（シーンには保存しません）###UI state preview")) {
                for (const auto& [name,value]:defaults.values) {
                    float draft=overrides_.contains(name)?overrides_.at(name):value;
                    ImGui::PushID(name.c_str()); ImGui::SetNextItemWidth(130);
                    if (ImGui::DragFloat(name.c_str(),&draft,.1f,-100000,100000,"%.2f",ImGuiSliderFlags_AlwaysClamp)) overrides_[name]=draft;
                    ImGui::PopID();
                }
                if (ImGui::Button("状態プレビューを初期化###Reset UI preview state")) overrides_.clear();
                ImGui::TreePop();
            }
            preview_=std::move(defaults);
            for (const auto& [name,value]:overrides_) preview_.values[name]=value;
            preview_.values["intro"]=1;
            float duration=1;
            for(const auto& object:layout.objects) if(object.animation) for(const auto& track:object.animation->tracks)
                if(!track.keys.empty()) duration=std::max(duration,track.delay+track.keys.back().time);
            ImGui::Checkbox("UIアニメーションの時刻プレビュー（保存しません）###Animation preview",&animationPreview_);
            if(animationPreview_) {
                if(ImGui::Button(playing_?"プレビューを停止###Preview playback":"プレビューを再生###Preview playback")) {
                    playing_=!playing_;if(playing_ && previewTime_>=duration) previewTime_=0;
                }
                ImGui::SameLine();ImGui::SetNextItemWidth(240);ImGui::SliderFloat("時刻（秒）###Animation preview time",&previewTime_,0,duration,"%.2f");
                if(playing_) {previewTime_=std::min(duration,previewTime_+ImGui::GetIO().DeltaTime);if(previewTime_>=duration) playing_=false;}
                for(const auto& object:layout.objects) if(object.animation) for(const auto& track:object.animation->tracks) preview_.values[track.clock]=previewTime_;
            }
            ImGui::BeginDisabled(!enabled || animationPreview_ || UiLayoutTools::Selection(layout,state).size()<2);
            static constexpr const char* labels[]{"左揃え","左右中央","右揃え","上揃え","上下中央","下揃え","横に等間隔","縦に等間隔"};
            for(int i=0;i<8;++i) {
                if(i%4) ImGui::SameLine();
                if(ImGui::Button(labels[i])) state.RequestComponentBatch(UiLayoutTools::Apply(layout,state,resolution_[0],resolution_[1],preview_,static_cast<UiLayoutTools::Arrange>(i)),"ui/arrange");
            }
            ImGui::EndDisabled();
            if(state.SelectedIds().size()>1) ImGui::TextUnformatted("整列は同じ親のUIを選択して操作します。等間隔は3個以上で使用できます。");
            if(state.SingleSelection() && ImGui::TreeNode("アンカー配置（見た目の位置を維持）###Anchor presets")) {
                const auto found=std::find_if(layout.objects.begin(),layout.objects.end(),[&](const auto& object){return object.id==state.SelectedId();});
                ImGui::BeginDisabled(!enabled || animationPreview_ || found==layout.objects.end() || !found->rectTransform);
                static constexpr const char* anchors[]{"左上","上中央","右上","左中央","中央","右中央","左下","下中央","右下"};
                for(int i=0;i<9;++i) {
                    if(i%3) ImGui::SameLine();
                    if(ImGui::Button(anchors[i]) && found!=layout.objects.end()) {
                        if(auto candidate=UiLayoutTools::Anchor(layout,*found,resolution_[0],resolution_[1],preview_,{(i%3)*.5f,(i/3)*.5f}))
                            state.RequestComponents(std::move(*candidate),"ui/anchor");
                    }
                }
                ImGui::EndDisabled(); ImGui::TreePop();
            }
            ImGui::TextUnformatted("ドラッグ: 移動・サイズ変更 / 青い丸: 回転 / Alt＋緑の丸: ピボット / 中ボタン: パン / Shift: スナップ解除");
            if (ImGui::BeginChild("Canvas viewport",{0,0},ImGuiChildFlags_Borders,ImGuiWindowFlags_HorizontalScrollbar)) {
                if (resetScroll_) {ImGui::SetScrollX(0); ImGui::SetScrollY(0); resetScroll_=false;}
                if (ImGui::IsWindowHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
                    ImGui::SetScrollX(ImGui::GetScrollX()-ImGui::GetIO().MouseDelta.x);
                    ImGui::SetScrollY(ImGui::GetScrollY()-ImGui::GetIO().MouseDelta.y);
                }
                const auto position=ImGui::GetCursorScreenPos();
                const ImVec2 size{resolution_[0]*zoom_,resolution_[1]*zoom_};
                if (texture) ImGui::Image(static_cast<ImTextureID>(texture),size);
                else ImGui::Dummy(size);
                const SceneViewport viewport{position.x,position.y,size.x,size.y};
                canvas_.SetPreviewState(preview_,zoom_); canvas_.SetSnap(snap_,grid_);
                canvas_.DrawLayout(layout,state,viewport,enabled && !animationPreview_);
                auto* draw=ImGui::GetWindowDrawList();
                for(unsigned int x=0;x<=resolution_[0];x+=100) {
                    const float px=position.x+x*zoom_; draw->AddLine({px,position.y},{px,position.y+5},IM_COL32(150,150,150,255));
                    draw->AddText({px+2,position.y+5},IM_COL32(180,180,180,255),std::to_string(x).c_str());
                }
                for(unsigned int y=100;y<=resolution_[1];y+=100) {
                    const float py=position.y+y*zoom_; draw->AddLine({position.x,py},{position.x+5,py},IM_COL32(150,150,150,255));
                    draw->AddText({position.x+5,py},IM_COL32(180,180,180,255),std::to_string(y).c_str());
                }
            }
            ImGui::EndChild(); ImGui::End();
        }
    private:
        UiCanvasPanel canvas_;
        SceneRuntime::UiState preview_;
        std::map<std::string,float> overrides_;
        std::array<unsigned int,2> resolution_{1280,720};
        float zoom_=1,grid_=10;
        bool snap_=false,visible_=false,resetScroll_=false;
        bool animationPreview_=false,playing_=false;
        float previewTime_=0;
    };
}
