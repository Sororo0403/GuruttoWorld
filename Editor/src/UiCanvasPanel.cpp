#include "UiCanvasPanel.h"
#include "UiLayoutTools.h"
#include "UiCanvasTransform.h"
#include <SceneRuntime/SceneCanvas.h>
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
    if(sceneWorld_) {
        const auto* root=SceneRuntime::SceneCanvas::Root(layout,object);
        if(!root) {SceneRuntime::UiRect rect; rect.visible=false; return rect;}
        return SceneRuntime::SceneUi::Resolve(layout,object,static_cast<unsigned int>(root->canvas->referenceSize[0]),static_cast<unsigned int>(root->canvas->referenceSize[1]),preview_);
    }
    auto rect=SceneRuntime::SceneUi::Resolve(layout,object,static_cast<unsigned int>(viewport.width/displayScale_),static_cast<unsigned int>(viewport.height/displayScale_),preview_);
    for(size_t axis=0;axis<2;++axis) {rect.position[axis]*=displayScale_; rect.size[axis]*=displayScale_;}
    rect.scale*=displayScale_; return rect;
}
std::optional<std::array<float,2>> UiCanvasPanel::Mouse(const SceneRuntime::ScenePlacement& object,const SceneViewport& viewport,const std::array<float,2>& mouse) const {
    if(!sceneWorld_) return mouse;
    const auto matrix=SceneRuntime::SceneCanvas::Matrix(*sceneWorld_,object);
    if(!matrix || !viewport.Valid()) return std::nullopt;
    return SceneRuntime::SceneCanvas::Intersect(*matrix,*sceneCamera_,(mouse[0]-viewport.x)/viewport.width*2-1,1-(mouse[1]-viewport.y)/viewport.height*2);
}
std::optional<std::array<float,2>> UiCanvasPanel::Point(const SceneRuntime::ScenePlacement& object,const SceneRuntime::UiRect& rect,const SceneViewport& viewport,float x,float y) const {
    const auto point=Corner(rect,sceneWorld_?SceneViewport{}:viewport,x,y);
    if(!sceneWorld_) return std::array<float,2>{point.x,point.y};
    const auto matrix=SceneRuntime::SceneCanvas::Matrix(*sceneWorld_,object);
    if(!matrix) return std::nullopt;
    using namespace DirectX;
    XMFLOAT4 clip;
    XMStoreFloat4(&clip,XMVector4Transform(XMVectorSet(point.x,point.y,0,1),XMLoadFloat4x4(&*matrix)*sceneCamera_->GetViewMatrix()*sceneCamera_->GetProjectionMatrix()));
    if(clip.w<=1e-5f || clip.z<0 || clip.z>clip.w) return std::nullopt;
    return viewport.ToScreen(clip.x/clip.w,clip.y/clip.w);
}
void UiCanvasPanel::SelectAndBegin(const SceneRuntime::SceneLayout& layout,EditState& state,const SceneViewport& viewport) {
    const auto mouse=ImGui::GetIO().MousePos;
    const auto selected=std::find_if(layout.objects.begin(),layout.objects.end(),[&](const auto& p){return p.id==state.SelectedId();});
    if(selected!=layout.objects.end() && selected->rectTransform) {
        const auto drawHandles=[&]() {
        const auto rect=Resolve(layout,*selected,viewport);
        if(!rect.visible || rect.opacity<=.001f) return;
        const auto a=Point(*selected,rect,viewport,0,0),b=Point(*selected,rect,viewport,1,0),c=Point(*selected,rect,viewport,1,1),d=Point(*selected,rect,viewport,0,1);
        if(!a || !b || !c || !d) return;
        const auto vector=[](const auto& point){return ImVec2{(*point)[0],(*point)[1]};};
        auto* draw=ImGui::GetWindowDrawList();
        draw->AddQuad(vector(a),vector(b),vector(c),vector(d),IM_COL32(255,190,40,255),2.0f);
        bool handle=false;
        const std::array<std::array<float,2>,8> handles{{{0,0},{.5f,0},{1,0},{1,.5f},{1,1},{.5f,1},{0,1},{0,.5f}}};
        for (const auto& position:handles) {
            const auto projected=Point(*selected,rect,viewport,position[0],position[1]);
            if(!projected) continue;
            const ImVec2 point{(*projected)[0],(*projected)[1]};
            draw->AddRectFilled({point.x-4,point.y-4},{point.x+4,point.y+4},IM_COL32(255,190,40,255));
            if (std::abs(mouse.x-point.x)<7 && std::abs(mouse.y-point.y)<7) {handle=true; handle_={position[0]*2-1,position[1]*2-1};}
        }
        const auto topProjected=Point(*selected,rect,viewport,.5f,0);
        const auto rotationProjected=Point(*selected,rect,viewport,.5f,rect.size[1]>0?-24/rect.size[1]:0);
        const auto pivotProjected=Point(*selected,rect,viewport,selected->rectTransform->pivot[0],selected->rectTransform->pivot[1]);
        if(!topProjected || !rotationProjected || !pivotProjected) return;
        const ImVec2 top{(*topProjected)[0],(*topProjected)[1]},rotationPoint{(*rotationProjected)[0],(*rotationProjected)[1]};
        draw->AddLine(top,rotationPoint,IM_COL32(100,210,255,255),1);
        draw->AddCircleFilled(rotationPoint,5,IM_COL32(100,210,255,255));
        const bool rotationHandle=std::abs(mouse.x-rotationPoint.x)<8 && std::abs(mouse.y-rotationPoint.y)<8;
        const ImVec2 pivotPoint{(*pivotProjected)[0],(*pivotProjected)[1]};
        draw->AddCircle(pivotPoint,6,IM_COL32(120,255,140,255));
        const bool pivotHandle=ImGui::GetIO().KeyAlt && std::abs(mouse.x-pivotPoint.x)<9 && std::abs(mouse.y-pivotPoint.y)<9;
        if(ImGui::IsWindowHovered() && viewport.Contains(mouse.x,mouse.y) && !ImGui::GetIO().KeyCtrl &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Right) && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && (handle || rotationHandle || pivotHandle)) {
            const auto local=Mouse(*selected,viewport,{mouse.x,mouse.y}); if(!local) return;
            start_=*selected; mouse_=*local; scale_=rect.scale;
            rotation_=rect.rotation-selected->rectTransform->rotation;
            pivot_=pivotHandle; rotate_=!pivot_ && rotationHandle; resize_=!pivot_ && !rotate_;
            center_={(sceneWorld_?0:viewport.x)+rect.position[0]+rect.size[0]*.5f,(sceneWorld_?0:viewport.y)+rect.position[1]+rect.size[1]*.5f}; startRect_=rect;
        }
        };
        drawHandles();
    }
    if(!start_ && ImGui::IsWindowHovered() && viewport.Contains(mouse.x,mouse.y) && !ImGui::IsMouseDown(ImGuiMouseButton_Right) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        std::string hit;
        if(sceneWorld_) {
            for(auto i=layout.objects.rbegin();i!=layout.objects.rend();++i) {
                const auto local=Mouse(*i,viewport,{mouse.x,mouse.y});
                if(i->rectTransform && local && Resolve(layout,*i,viewport).Contains((*local)[0],(*local)[1])) {hit=i->id; break;}
            }
            if(hit.empty()) for(auto i=layout.objects.rbegin();i!=layout.objects.rend();++i) {
                if(!i->canvas || !i->canvas->enabled || SceneRuntime::SceneCanvas::Root(layout,*i)!=&*i) continue;
                const auto local=Mouse(*i,viewport,{mouse.x,mouse.y});
                if(local && (*local)[0]>=0 && (*local)[1]>=0 && (*local)[0]<=i->canvas->referenceSize[0] && (*local)[1]<=i->canvas->referenceSize[1]) {hit=i->id;break;}
            }
        } else hit=SceneRuntime::SceneUi::Hit(layout,static_cast<unsigned int>(viewport.width/displayScale_),static_cast<unsigned int>(viewport.height/displayScale_),(mouse.x-viewport.x)/displayScale_,(mouse.y-viewport.y)/displayScale_,preview_,false);
        if(!hit.empty()) {
            consumed_=true;
            const auto object=std::find_if(layout.objects.begin(),layout.objects.end(),[&](const auto& item){return item.id==hit;});
            if (ImGui::GetIO().KeyCtrl) { state.Select(hit,true); return; }
            if (std::find(state.SelectedIds().begin(),state.SelectedIds().end(),hit)==state.SelectedIds().end()) state.Select(hit);
            if (object!=layout.objects.end() && object->rectTransform) {
                const auto rect=Resolve(layout,*object,viewport);
                const auto local=Mouse(*object,viewport,{mouse.x,mouse.y}); if(!local) return;
                start_=*object; mouse_=*local; scale_=rect.scale;
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
    if (!candidate.SameComponents(*start_)) {
        if(!resize_ && !rotate_ && !pivot_ && dragSelection_.size()>1) {
            auto batch=dragSelection_;
            const std::array<float,2> delta{c.position[0]-start_->rectTransform->position[0],c.position[1]-start_->rectTransform->position[1]};
            for(auto& object:batch) for(size_t axis=0;axis<2;++axis) object.rectTransform->position[axis]+=delta[axis];
            state.RequestComponentBatch(std::move(batch),state.Interaction());
        } else state.RequestComponents(std::move(candidate),state.Interaction());
    }
    if(!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {start_.reset();dragSelection_.clear();}
}
void UiCanvasPanel::Draw(const SceneRuntime::SceneWorld& world,EditState& state,const SceneViewport& viewport,bool enabled) {
    sceneWorld_=nullptr; sceneCamera_=nullptr;
    DrawLayout(world.Layout(),state,viewport,enabled);
}
void UiCanvasPanel::DrawLayout(const SceneRuntime::SceneLayout& layout,EditState& state,const SceneViewport& viewport,bool enabled) {
    consumed_=false;
    if(!enabled || !viewport.Valid()) {start_.reset();dragSelection_.clear(); return;}
    if(!start_) {
        SelectAndBegin(layout,state,viewport);
        if(start_) dragSelection_=UiLayoutTools::Selection(layout,state);
    }
    if(start_) {const auto mouse=ImGui::GetIO().MousePos; const auto local=Mouse(*start_,viewport,{mouse.x,mouse.y}); if(local) UpdateDrag(state,*local); else if(!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {start_.reset();dragSelection_.clear();}}
}
void UiCanvasPanel::DrawScene(const SceneRuntime::SceneWorld& world,const Engine::Camera& camera,EditState& state,const SceneViewport& viewport,bool enabled) {
    sceneWorld_=&world; sceneCamera_=&camera;
    if(viewport.Valid()) {
        auto* draw=ImGui::GetWindowDrawList();
        draw->PushClipRect({viewport.x,viewport.y},{viewport.x+viewport.width,viewport.y+viewport.height},true);
        for(const auto& object:world.Layout().objects) {
            if(!object.canvas || !object.canvas->enabled || SceneRuntime::SceneCanvas::Root(world.Layout(),object)!=&object) continue;
            SceneRuntime::UiRect rect; rect.size=object.canvas->referenceSize;
            const auto a=Point(object,rect,viewport,0,0),b=Point(object,rect,viewport,1,0),c=Point(object,rect,viewport,1,1),d=Point(object,rect,viewport,0,1);
            if(a && b && c && d) draw->AddQuad({(*a)[0],(*a)[1]},{(*b)[0],(*b)[1]},{(*c)[0],(*c)[1]},{(*d)[0],(*d)[1]},IM_COL32(130,190,210,220));
        }
        DrawLayout(world.Layout(),state,viewport,enabled);
        draw->PopClipRect();
    } else DrawLayout(world.Layout(),state,viewport,false);
}
}
