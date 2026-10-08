#include "UiCanvasPanel.h"
#include <imgui.h>
#include <cmath>
namespace {
ImVec2 Corner(const SceneRuntime::UiRect& rect,const Editor::SceneViewport& v,float x,float y) {
    x=(x-0.5f)*rect.size[0]; y=(y-0.5f)*rect.size[1];
    return {v.x+rect.position[0]+rect.size[0]*0.5f+x*std::cos(rect.rotation)-y*std::sin(rect.rotation),
        v.y+rect.position[1]+rect.size[1]*0.5f+x*std::sin(rect.rotation)+y*std::cos(rect.rotation)};
}
}
namespace Editor {
void UiCanvasPanel::SelectAndBegin(const SceneRuntime::SceneLayout& layout,EditState& state,const SceneViewport& viewport) {
    const auto mouse=ImGui::GetIO().MousePos;
    const auto selected=std::find_if(layout.objects.begin(),layout.objects.end(),[&](const auto& p){return p.id==state.SelectedId();});
    if(selected!=layout.objects.end() && selected->rectTransform) {
        const auto rect=SceneRuntime::SceneUi::Resolve(layout,*selected,static_cast<unsigned int>(viewport.width),static_cast<unsigned int>(viewport.height));
        const auto a=Corner(rect,viewport,0,0),b=Corner(rect,viewport,1,0),c=Corner(rect,viewport,1,1),d=Corner(rect,viewport,0,1);
        auto* draw=ImGui::GetWindowDrawList();
        draw->AddQuad(a,b,c,d,IM_COL32(255,190,40,255),2.0f);
        draw->AddRectFilled({c.x-6,c.y-6},{c.x+6,c.y+6},IM_COL32(255,190,40,255));
        const bool handle=std::abs(mouse.x-c.x)<9 && std::abs(mouse.y-c.y)<9;
        if(ImGui::IsWindowHovered() && viewport.Contains(mouse.x,mouse.y) && !ImGui::GetIO().KeyCtrl &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) && handle) {
            start_=*selected; mouse_={mouse.x,mouse.y}; scale_=rect.scale;
            rotation_=rect.rotation-selected->rectTransform->rotation; resize_=true;
        }
    }
    if(!start_ && ImGui::IsWindowHovered() && viewport.Contains(mouse.x,mouse.y) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const auto hit=SceneRuntime::SceneUi::Hit(layout,static_cast<unsigned int>(viewport.width),static_cast<unsigned int>(viewport.height),mouse.x-viewport.x,mouse.y-viewport.y,{},false);
        if(!hit.empty()) {
            const auto object=std::find_if(layout.objects.begin(),layout.objects.end(),[&](const auto& item){return item.id==hit;});
            if (ImGui::GetIO().KeyCtrl) { state.Select(hit,true); return; }
            if (std::find(state.SelectedIds().begin(),state.SelectedIds().end(),hit)==state.SelectedIds().end()) state.Select(hit);
            if (object!=layout.objects.end() && object->rectTransform) {
                const auto rect=SceneRuntime::SceneUi::Resolve(layout,*object,static_cast<unsigned int>(viewport.width),static_cast<unsigned int>(viewport.height));
                start_=*object; mouse_={mouse.x,mouse.y}; scale_=rect.scale;
                rotation_=rect.rotation-object->rectTransform->rotation; resize_=false;
            }
        }
    }
}
void UiCanvasPanel::UpdateDrag(EditState& state,const std::array<float,2>& mouse) {
    const float x=(mouse[0]-mouse_[0])/scale_,y=(mouse[1]-mouse_[1])/scale_;
    auto candidate=*start_; auto& c=*candidate.rectTransform;
    const float angle=rotation_+(resize_?c.rotation:0);
    const float dx=x*std::cos(angle)+y*std::sin(angle),dy=-x*std::sin(angle)+y*std::cos(angle);
    if(resize_) {
        const auto old=c.size;
        c.size={std::clamp(old[0]+dx,0.0f,100000.0f),std::clamp(old[1]+dy,0.0f,100000.0f)};
        const float sx=c.size[0]-old[0],sy=c.size[1]-old[1];
        c.position[0]+=(sx*std::cos(c.rotation)-sy*std::sin(c.rotation))*0.5f-(0.5f-c.pivot[0])*sx;
        c.position[1]+=(sx*std::sin(c.rotation)+sy*std::cos(c.rotation))*0.5f-(0.5f-c.pivot[1])*sy;
    } else {
        c.position[0]=std::clamp(c.position[0]+dx,-100000.0f,100000.0f);
        c.position[1]=std::clamp(c.position[1]+dy,-100000.0f,100000.0f);
    }
    state.SetInteraction("ui/drag/"+start_->id);
    if (!candidate.SameComponents(*start_)) state.RequestComponents(std::move(candidate),state.Interaction());
    if(!ImGui::IsMouseDown(ImGuiMouseButton_Left)) start_.reset();
}
void UiCanvasPanel::Draw(const SceneRuntime::SceneWorld& world,EditState& state,const SceneViewport& viewport,bool enabled) {
    DrawLayout(world.Layout(),state,viewport,enabled);
}
void UiCanvasPanel::DrawLayout(const SceneRuntime::SceneLayout& layout,EditState& state,const SceneViewport& viewport,bool enabled) {
    if(!enabled || !viewport.Valid()) {start_.reset(); return;}
    if(!start_) SelectAndBegin(layout,state,viewport);
    if(start_) {const auto mouse=ImGui::GetIO().MousePos; UpdateDrag(state,{mouse.x,mouse.y});}
}
}
