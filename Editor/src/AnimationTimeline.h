#pragma once
#include "EditState.h"
#include <SceneRuntime/Animation.h>
#include <imgui.h>
#include <cmath>
#include <limits>

namespace Editor {
class AnimationTimeline final {
public:
    static bool MoveKey(SceneRuntime::AnimationTrack& track,size_t index,float time,float value,size_t channel) {
        if(index>=track.keys.size() || channel>=4 || !std::isfinite(time) || !std::isfinite(value)) return false;
        auto candidate=track;
        const float low=index ? std::nextafter(track.keys[index-1].time,600.0f):0;
        const float high=index+1<track.keys.size()?std::nextafter(track.keys[index+1].time,0.0f):600;
        if(low>high) return false;
        candidate.keys[index].time=std::clamp(time,low,high);
        if(track.property=="opacity" && channel==0) value=std::clamp(value,0.0f,1.0f);
        else if(track.property=="uiSize" && channel<2) value=std::max(0.0f,value);
        candidate.keys[index].value[channel]=std::clamp(value,-100000.0f,100000.0f);
        if(!SceneRuntime::Animation::Valid(candidate)) return false;
        track=std::move(candidate);return true;
    }
    static bool Draw(EditState& state,SceneRuntime::AnimationTrack& track) {
        if(track.keys.empty()) return false;
        const auto id=ImGui::GetID("Animation timeline");auto& view=views_[id];
        ImGui::Combo("カーブの値###Curve channel",&view.channel,"X\0Y\0Z\0W\0");
        const ImVec2 origin=ImGui::GetCursorScreenPos(),size{std::max(150.0f,ImGui::GetContentRegionAvail().x),150};
        ImGui::InvisibleButton("Animation timeline",size);
        const bool active=ImGui::IsItemActive(),hovered=ImGui::IsItemHovered();
        if(!active) {
            view.duration=std::max(1.0f,track.keys.back().time*1.1f);view.low=track.keys.front().value[view.channel];view.high=view.low;
            for(const auto& key:track.keys) {view.low=std::min(view.low,key.value[view.channel]);view.high=std::max(view.high,key.value[view.channel]);}
            const auto padding=std::max(1.0f,(view.high-view.low)*.15f);view.low-=padding;view.high+=padding;
        }
        const auto point=[&](float time,float value){return ImVec2{origin.x+8+time/view.duration*(size.x-16),origin.y+size.y-8-(value-view.low)/(view.high-view.low)*(size.y-16)};};
        auto* draw=ImGui::GetWindowDrawList();draw->AddRectFilled(origin,{origin.x+size.x,origin.y+size.y},IM_COL32(24,30,40,255));
        draw->PushClipRect(origin,{origin.x+size.x,origin.y+size.y},true);
        for(int i=1;i<5;++i) {
            const float x=origin.x+size.x*i/5;draw->AddLine({x,origin.y},{x,origin.y+size.y},IM_COL32(60,70,85,255));
            const float y=origin.y+size.y*i/5;draw->AddLine({origin.x,y},{origin.x+size.x,y},IM_COL32(60,70,85,255));
        }
        std::optional<ImVec2> previous;
        for(int i=0;i<=96;++i) {
            const float time=view.duration*i/96;
            const auto sample=SceneRuntime::Animation::Sample(track,{{track.clock,time+track.delay}});
            if(sample) {const auto p=point(time,(*sample)[view.channel]);if(previous) draw->AddLine(*previous,p,IM_COL32(90,210,255,255),2);previous=p;}
        }
        const auto mouse=ImGui::GetIO().MousePos;
        if(hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            view.key.reset();float nearest=100;
            for(size_t index=0;index<track.keys.size();++index) {
                const auto p=point(track.keys[index].time,track.keys[index].value[view.channel]);
                const float distance=(p.x-mouse.x)*(p.x-mouse.x)+(p.y-mouse.y)*(p.y-mouse.y);
                if(distance<nearest) {view.key=index;nearest=distance;}
            }
        }
        bool edited=false;
        if(active && view.key && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            const float time=(mouse.x-origin.x-8)/(size.x-16)*view.duration;
            const float value=view.low+(origin.y+size.y-8-mouse.y)/(size.y-16)*(view.high-view.low);
            auto before=track;
            edited=MoveKey(track,*view.key,time,value,view.channel) && track!=before;
            state.SetInteraction("animation/timeline/"+std::to_string(id));
        }
        for(size_t index=0;index<track.keys.size();++index) {
            const auto p=point(track.keys[index].time,track.keys[index].value[view.channel]);
            draw->AddCircleFilled(p,5,view.key==index?IM_COL32(255,200,70,255):IM_COL32(255,255,255,255));
        }
        if(!ImGui::IsMouseDown(ImGuiMouseButton_Left)) view.key.reset();
        draw->PopClipRect();
        ImGui::Text("0〜%.2f秒 / 値 %.2f〜%.2f",view.duration,view.low,view.high);
        ImGui::TextWrapped("丸いキーをドラッグして時刻と値を編集します。キーの順序は維持します。数値は下の項目でも調整できます。");
        return edited;
    }
private:
    struct View {int channel=0;float duration=1,low=-1,high=1;std::optional<size_t> key;};
    inline static std::map<ImGuiID,View> views_;
};
}
