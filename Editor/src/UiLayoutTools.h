#pragma once
#include "EditState.h"
#include <SceneRuntime/SceneUi.h>
#include <cmath>

namespace Editor {
struct UiLayoutTools {
    enum class Arrange {Left,CenterX,Right,Top,CenterY,Bottom,Horizontal,Vertical};
    static std::vector<SceneRuntime::ScenePlacement> Selection(const SceneRuntime::SceneLayout& layout,const EditState& state) {
        std::vector<SceneRuntime::ScenePlacement> result;
        for(const auto& object:layout.objects) if(state.IsSelected(object.id) && object.rectTransform && object.rectTransform->enabled) result.push_back(object);
        if(result.size()!=state.SelectedIds().size()) return {};
        // Siblings share a coordinate space; nested selections would otherwise move twice.
        if(!result.empty() && std::any_of(result.begin(),result.end(),[&](const auto& object){return object.parentId!=result.front().parentId;})) return {};
        return result;
    }
    static void Translate(SceneRuntime::ScenePlacement& object,const SceneRuntime::UiRect& rect,const std::array<float,2>& delta) {
        const float rotation=rect.rotation-object.rectTransform->rotation;
        object.rectTransform->position[0]+=(delta[0]*std::cos(rotation)+delta[1]*std::sin(rotation))/rect.scale;
        object.rectTransform->position[1]+=(-delta[0]*std::sin(rotation)+delta[1]*std::cos(rotation))/rect.scale;
    }
    static std::optional<SceneRuntime::ScenePlacement> Anchor(const SceneRuntime::SceneLayout& layout,const SceneRuntime::ScenePlacement& object,
        unsigned int width,unsigned int height,const SceneRuntime::UiState& preview,const std::array<float,2>& value) {
        if(!object.rectTransform) return {};
        const auto parent=std::find_if(layout.objects.begin(),layout.objects.end(),[&](const auto& item){return item.id==object.parentId;});
        if(parent==layout.objects.end()) return {};
        const auto rect=SceneRuntime::SceneUi::Resolve(layout,*parent,width,height,preview);
        if(!rect.visible || rect.scale<=0) return {};
        auto result=object; auto& c=*result.rectTransform;
        for(size_t axis=0;axis<2;++axis) {
            c.size[axis]+=rect.size[axis]*(c.anchorMax[axis]-c.anchorMin[axis])/rect.scale;
            c.position[axis]+=rect.size[axis]*(c.anchorMin[axis]-value[axis])/rect.scale;
        }
        c.anchorMin=value; c.anchorMax=value;
        return result;
    }
    static std::vector<SceneRuntime::ScenePlacement> Apply(const SceneRuntime::SceneLayout& layout,const EditState& state,
        unsigned int width,unsigned int height,const SceneRuntime::UiState& preview,Arrange action) {
        auto result=Selection(layout,state);
        if(result.size()<2) return {};
        struct Bounds {float min,max,center;size_t index;SceneRuntime::UiRect rect;};
        const bool horizontal=action==Arrange::Left || action==Arrange::CenterX || action==Arrange::Right || action==Arrange::Horizontal;
        const size_t axis=horizontal?0:1;
        std::vector<Bounds> bounds;
        for(size_t i=0;i<result.size();++i) {
            const auto rect=SceneRuntime::SceneUi::Resolve(layout,result[i],width,height,preview);
            if(!rect.visible || rect.scale<=0) return {};
            const float extent=.5f*(std::abs(std::cos(rect.rotation))*rect.size[axis]+std::abs(std::sin(rect.rotation))*rect.size[1-axis]);
            const float center=rect.position[axis]+rect.size[axis]*.5f;
            bounds.push_back({center-extent,center+extent,center,i,rect});
        }
        const auto left=std::min_element(bounds.begin(),bounds.end(),[](const auto& a,const auto& b){return a.min<b.min;})->min;
        const auto right=std::max_element(bounds.begin(),bounds.end(),[](const auto& a,const auto& b){return a.max<b.max;})->max;
        const bool distribute=action==Arrange::Horizontal || action==Arrange::Vertical;
        if(distribute && bounds.size()<3) return {};
        float next=left,gap=0;
        if(distribute) {
            std::stable_sort(bounds.begin(),bounds.end(),[](const auto& a,const auto& b){return a.center<b.center;});
            float total=0;for(const auto& b:bounds) total+=b.max-b.min;
            gap=(right-left-total)/(bounds.size()-1);
        }
        for(const auto& b:bounds) {
            float delta=0;
            if(distribute) {delta=next-b.min;next+=b.max-b.min+gap;}
            else if(action==Arrange::Left || action==Arrange::Top) delta=left-b.min;
            else if(action==Arrange::Right || action==Arrange::Bottom) delta=right-b.max;
            else delta=(left+right)*.5f-b.center;
            std::array<float,2> movement{};movement[axis]=delta;
            Translate(result[b.index],b.rect,movement);
        }
        return result;
    }
};
}
