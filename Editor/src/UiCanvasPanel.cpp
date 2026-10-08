#include "UiCanvasPanel.h"
#include "UiCanvasTransform.h"
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
SceneRuntime::UiRect UiCanvasPanel::Resolve(const SceneRuntime::SceneLayout& layout,const SceneRuntime::ScenePlacement& object,const SceneViewport& viewport) const {
    auto rect=SceneRuntime::SceneUi::Resolve(layout,object,static_cast<unsigned int>(viewport.width/displayScale_),static_cast<unsigned int>(viewport.height/displayScale_),preview_);
    for(size_t axis=0;axis<2;++axis) {rect.position[axis]*=displayScale_; rect.size[axis]*=displayScale_;}
    rect.scale*=displayScale_; return rect;
}
void UiCanvasPanel::SelectAndBegin(const SceneRuntime::SceneLayout& layout,EditState& state,const SceneViewport& viewport) {
    const auto mouse=ImGui::GetIO().MousePos;
    const auto selected=std::find_if(layout.objects.begin(),layout.objects.end(),[&](const auto& p){return p.id==state.SelectedId();});
    if(selected!=layout.objects.end() && selected->rectTransform) {
        const auto rect=Resolve(layout,*selected,viewport);
        const auto a=Corner(rect,viewport,0,0),b=Corner(rect,viewport,1,0),c=Corner(rect,viewport,1,1),d=Corner(rect,viewport,0,1);
        auto* draw=ImGui::GetWindowDrawList();
        draw->AddQuad(a,b,c,d,IM_COL32(255,190,40,255),2.0f);
        bool handle=false;
        const std::array<std::array<float,2>,8> handles{{{0,0},{.5f,0},{1,0},{1,.5f},{1,1},{.5f,1},{0,1},{0,.5f}}};
        for (const auto& position:handles) {
            const auto point=Corner(rect,viewport,position[0],position[1]);
            draw->AddRectFilled({point.x-4,point.y-4},{point.x+4,point.y+4},IM_COL32(255,190,40,255));
            if (std::abs(mouse.x-point.x)<7 && std::abs(mouse.y-point.y)<7) {handle=true; handle_={position[0]*2-1,position[1]*2-1};}
        }
        const auto top=Corner(rect,viewport,.5f,0);
        const auto rotationPoint=Corner(rect,viewport,.5f,rect.size[1]>0?-24/rect.size[1]:0);
        draw->AddLine(top,rotationPoint,IM_COL32(100,210,255,255),1);
        draw->AddCircleFilled(rotationPoint,5,IM_COL32(100,210,255,255));
        const bool rotationHandle=std::abs(mouse.x-rotationPoint.x)<8 && std::abs(mouse.y-rotationPoint.y)<8;
        const auto pivotPoint=Corner(rect,viewport,selected->rectTransform->pivot[0],selected->rectTransform->pivot[1]);
        draw->AddCircle(pivotPoint,6,IM_COL32(120,255,140,255));
        const bool pivotHandle=ImGui::GetIO().KeyAlt && std::abs(mouse.x-pivotPoint.x)<9 && std::abs(mouse.y-pivotPoint.y)<9;
        if(ImGui::IsWindowHovered() && viewport.Contains(mouse.x,mouse.y) && !ImGui::GetIO().KeyCtrl &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) && (handle || rotationHandle || pivotHandle)) {
            start_=*selected; mouse_={mouse.x,mouse.y}; scale_=rect.scale;
            rotation_=rect.rotation-selected->rectTransform->rotation;
            pivot_=pivotHandle; rotate_=!pivot_ && rotationHandle; resize_=!pivot_ && !rotate_;
            center_={viewport.x+rect.position[0]+rect.size[0]*.5f,viewport.y+rect.position[1]+rect.size[1]*.5f}; startRect_=rect;
        }
    }
    if(!start_ && ImGui::IsWindowHovered() && viewport.Contains(mouse.x,mouse.y) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const auto hit=SceneRuntime::SceneUi::Hit(layout,static_cast<unsigned int>(viewport.width/displayScale_),static_cast<unsigned int>(viewport.height/displayScale_),(mouse.x-viewport.x)/displayScale_,(mouse.y-viewport.y)/displayScale_,preview_,false);
        if(!hit.empty()) {
            const auto object=std::find_if(layout.objects.begin(),layout.objects.end(),[&](const auto& item){return item.id==hit;});
            if (ImGui::GetIO().KeyCtrl) { state.Select(hit,true); return; }
            if (std::find(state.SelectedIds().begin(),state.SelectedIds().end(),hit)==state.SelectedIds().end()) state.Select(hit);
            if (object!=layout.objects.end() && object->rectTransform) {
                const auto rect=Resolve(layout,*object,viewport);
                start_=*object; mouse_={mouse.x,mouse.y}; scale_=rect.scale;
                rotation_=rect.rotation-object->rectTransform->rotation; resize_=false; rotate_=false; pivot_=false;
            }
        }
    }
}
void UiCanvasPanel::UpdateDrag(EditState& state,const std::array<float,2>& mouse) {
    const float x=(mouse[0]-mouse_[0])/scale_,y=(mouse[1]-mouse_[1])/scale_;
    auto candidate=*start_; auto& c=*candidate.rectTransform;
    const float angle=rotation_+(resize_?c.rotation:0);
    const float dx=x*std::cos(angle)+y*std::sin(angle),dy=-x*std::sin(angle)+y*std::cos(angle);
    if (pivot_) {
        c=UiCanvasTransform::Pivot(c,startRect_,{mouse[0]-mouse_[0],mouse[1]-mouse_[1]});
    } else if (rotate_) {
        c.rotation+=std::atan2(mouse[1]-center_[1],mouse[0]-center_[0])-std::atan2(mouse_[1]-center_[1],mouse_[0]-center_[0]);
        if (snap_ && !ImGui::GetIO().KeyShift) c.rotation=std::round(c.rotation/(3.14159265f/12))*(3.14159265f/12);
    } else if(resize_) {
        c=UiCanvasTransform::Resize(c,rotation_,scale_,handle_,{mouse[0]-mouse_[0],mouse[1]-mouse_[1]});
    } else {
        c.position[0]=std::clamp(c.position[0]+dx,-100000.0f,100000.0f);
        c.position[1]=std::clamp(c.position[1]+dy,-100000.0f,100000.0f);
        if (snap_ && grid_>0 && !ImGui::GetIO().KeyShift) for (auto& value:c.position) value=std::round(value/grid_)*grid_;
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
