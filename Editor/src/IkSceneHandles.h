#pragma once
#include "EditState.h"
#include "SceneViewport.h"
#include "IkHandleTransform.h"
#include <Engine/Graphics/Camera.h>
#include <imgui.h>
#include <ImGuizmo.h>

namespace Editor {
class IkSceneHandles final {
public:
    bool Available(const SceneRuntime::SceneLayout& layout,const EditState& state) const {
        if(!state.SingleSelection() || !state.InspectedAsset().empty()) return false;
        const auto found=std::find_if(layout.objects.begin(),layout.objects.end(),[&](const auto& object){return object.id==state.SelectedId();});
        return found!=layout.objects.end() && found->animator && !found->animator->ik.empty();
    }
    bool Enabled() const {return enabled_;}
    bool IsDragging() const {return dragging_;}
    bool ConsumesMouse() const {return dragging_ || hovered_;}
    void Cancel() {dragging_=false;hovered_=false;}
    void Controls(const SceneRuntime::SceneLayout& layout,const EditState& state,const SceneViewport& viewport,bool enabled) {
        hovered_=false;
        if(!Available(layout,state) || !viewport.Valid()) {Cancel();return;}
        const auto found=std::find_if(layout.objects.begin(),layout.objects.end(),[&](const auto& object){return object.id==state.SelectedId();});
        const auto& constraints=found->animator->ik;
        if(selected_!=found->id) {selected_=found->id;index_=0;dragging_=false;}
        if(index_>=constraints.size()) index_=0;
        const auto cursor=ImGui::GetCursorScreenPos();ImGui::SetCursorScreenPos({viewport.x+8,viewport.y+8});
        ImGui::BeginDisabled(!enabled || dragging_);
        ImGui::Checkbox("IKハンドル###IK scene handles",&enabled_);hovered_|=ImGui::IsItemHovered();
        if(enabled_) {
            ImGui::SameLine();ImGui::SetNextItemWidth(150);
            if(ImGui::BeginCombo("制約###IK constraint",constraints[index_].name.c_str())) {
                for(size_t i=0;i<constraints.size();++i) if(ImGui::Selectable(constraints[i].name.c_str(),i==index_)) index_=i;
                ImGui::EndCombo();
            }
            hovered_|=ImGui::IsItemHovered();
            ImGui::SameLine();if(ImGui::RadioButton("目標###IK target",!hint_)) hint_=false;hovered_|=ImGui::IsItemHovered();
            ImGui::SameLine();if(ImGui::RadioButton("補助点###IK hint",hint_)) hint_=true;hovered_|=ImGui::IsItemHovered();
        }
        ImGui::EndDisabled();ImGui::SetCursorScreenPos(cursor);
    }
    void Draw(SceneRuntime::SceneWorld& world,const Engine::Camera& camera,EditState& state,const SceneViewport& viewport,bool active) {
        if(!active || !Available(world.Layout(),state) || !viewport.Valid()) {Cancel();ImGuizmo::Enable(false);return;}
        const auto found=std::find_if(world.Layout().objects.begin(),world.Layout().objects.end(),[&](const auto& object){return object.id==state.SelectedId();});
        if(index_>=found->animator->ik.size()) {Cancel();return;}
        if(hovered_ && !dragging_) {ImGuizmo::Enable(false);return;}
        DirectX::XMFLOAT4X4 matrix;
        if(!world.WorldMatrix(found->id,matrix)) {Cancel();return;}
        const auto target=IkHandleTransform::ToWorld(found->animator->ik[index_],matrix,hint_);
        if(!target) {Cancel();return;}
        DirectX::XMFLOAT4X4 handle,view,projection;
        DirectX::XMStoreFloat4x4(&handle,DirectX::XMMatrixTranslation((*target)[0],(*target)[1],(*target)[2]));
        DirectX::XMStoreFloat4x4(&view,camera.GetViewMatrix());DirectX::XMStoreFloat4x4(&projection,camera.GetProjectionMatrix());
        ImGuizmo::Enable(!ImGui::IsMouseDown(ImGuiMouseButton_Right));ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
        ImGuizmo::SetOrthographic(false);ImGuizmo::SetRect(viewport.x,viewport.y,viewport.width,viewport.height);
        ImGuizmo::PushID((found->id+"/ik/"+std::to_string(index_)+(hint_?"/hint":"/target")).c_str());
        const bool changed=ImGuizmo::Manipulate(&view._11,&projection._11,ImGuizmo::TRANSLATE,ImGuizmo::WORLD,&handle._11);
        dragging_=ImGuizmo::IsUsing();hovered_|=ImGuizmo::IsOver();ImGuizmo::PopID();
        const auto interaction="ik/"+found->id+"/"+std::to_string(index_)+(hint_?"/hint":"/target");
        if(dragging_) state.SetInteraction(interaction);
        if(changed) {
            auto candidate=*found;
            if(IkHandleTransform::Apply(candidate.animator->ik[index_],matrix,hint_,{handle._41,handle._42,handle._43}))
                state.RequestComponents(std::move(candidate),interaction);
        }
    }
private:
    bool enabled_=false,hint_=false,dragging_=false,hovered_=false;
    size_t index_=0;
    std::string selected_;
};
}
