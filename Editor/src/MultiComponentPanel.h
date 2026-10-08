#pragma once
#include "EditState.h"
#include "ProjectCatalog.h"
#include <imgui.h>
#include <imgui_internal.h>

namespace Editor {
class MultiComponentPanel final {
public:
    template<class Component,class Value>
    static bool Apply(std::vector<SceneRuntime::ScenePlacement>& objects,
        std::optional<Component> SceneRuntime::ScenePlacement::*component,Value Component::*field,const Value& value) {
        if(objects.empty() || std::any_of(objects.begin(),objects.end(),[&](const auto& object){return !(object.*component);})) return false;
        for(auto& object:objects) ((object.*component).value()).*field=value;
        return true;
    }
    static void Draw(EditState& state,const SceneRuntime::SceneLayout& layout,const ProjectCatalog* catalog) {
        std::vector<SceneRuntime::ScenePlacement> objects;
        for(const auto& object:layout.objects) if(state.IsSelected(object.id)) objects.push_back(object);
        if(objects.size()<2 || objects.size()!=state.SelectedIds().size()) return;
        bool changed=false;
        const auto group=[&](auto component,const char* title,auto draw) {
            if(std::any_of(objects.begin(),objects.end(),[&](const auto& object){return !(object.*component);})) return;
            if(!ImGui::CollapsingHeader(title)) return;
            ImGui::PushID(title);
            using Component=typename std::remove_reference_t<decltype(objects.front().*component)>::value_type;
            changed=Field(state,objects,component,&Component::enabled,"有効###Enabled",[](auto& value){return ImGui::Checkbox("有効###Enabled",&value);}) || changed;
            draw(component);ImGui::PopID();
        };
        const auto scalar=[&](auto component,auto field,const char* label,float low=0,float high=100000) {
            changed=Field(state,objects,component,field,label,[&](auto& value){return ImGui::DragFloat(label,&value,.05f,low,high,"%.3f",ImGuiSliderFlags_AlwaysClamp);}) || changed;
        };
        const auto boolean=[&](auto component,auto field,const char* label) {
            changed=Field(state,objects,component,field,label,[&](auto& value){return ImGui::Checkbox(label,&value);}) || changed;
        };
        const auto vector=[&](auto component,auto field,const char* label,float low=-100000,float high=100000) {
            changed=Field(state,objects,component,field,label,[&](auto& value){
                if constexpr(std::tuple_size_v<std::remove_reference_t<decltype(value)>> == 2) return ImGui::DragFloat2(label,value.data(),.1f,low,high,"%.3f",ImGuiSliderFlags_AlwaysClamp);
                else return ImGui::DragFloat3(label,value.data(),.1f,low,high,"%.3f",ImGuiSliderFlags_AlwaysClamp);
            }) || changed;
        };
        const auto asset=[&](auto component,auto field,const char* label,AssetKind kind,bool empty) {
            changed=Field(state,objects,component,field,label,[&](auto& value){
                bool edited=false;
                if(ImGui::BeginCombo(label,ProjectCatalog::Text(value).c_str())) {
                    if(empty && ImGui::Selectable("モデルの設定",value.empty())) {value.clear();edited=true;}
                    if(catalog) for(const auto& entry:catalog->Assets()) if(entry.kind==kind && ImGui::Selectable(ProjectCatalog::Text(entry.path).c_str(),entry.path==value)) {value=entry.path;edited=true;}
                    ImGui::EndCombo();
                } return edited;
            }) || changed;
        };
        using P=SceneRuntime::ScenePlacement;
        group(&P::meshRenderer,"共通メッシュ描画###Shared mesh",[&](auto c){asset(c,&SceneRuntime::MeshRendererComponent::model,"モデル###Model",AssetKind::Model,false);});
        group(&P::material,"共通Material割り当て###Shared material",[&](auto c){asset(c,&SceneRuntime::MaterialComponent::asset,"Material###Material",AssetKind::Material,true);});
        group(&P::rotator,"共通自動回転###Shared rotator",[&](auto c){vector(c,&SceneRuntime::RotatorComponent::angularVelocity,"角速度（度/秒）###Angular velocity");});
        group(&P::playerController,"共通プレイヤー設定###Shared player",[&](auto c){
            scalar(c,&SceneRuntime::PlayerControllerComponent::moveSpeed,"移動速度###Move speed");
            scalar(c,&SceneRuntime::PlayerControllerComponent::jumpSpeed,"ジャンプ速度###Jump speed");
            boolean(c,&SceneRuntime::PlayerControllerComponent::useGravity,"重力を使用###Use gravity");
            scalar(c,&SceneRuntime::PlayerControllerComponent::gravity,"重力###Gravity");
        });
        group(&P::boxCollider,"共通Collider###Shared collider",[&](auto c){
            vector(c,&SceneRuntime::BoxColliderComponent::center,"中心###Center");vector(c,&SceneRuntime::BoxColliderComponent::size,"サイズ###Size",.001f);
            boolean(c,&SceneRuntime::BoxColliderComponent::isTrigger,"トリガー###Trigger");
        });
        group(&P::rigidBody,"共通RigidBody###Shared rigidbody",[&](auto c){
            scalar(c,&SceneRuntime::RigidBodyComponent::mass,"質量###Mass",.001f);
            scalar(c,&SceneRuntime::RigidBodyComponent::friction,"摩擦###Friction",0,1);
            scalar(c,&SceneRuntime::RigidBodyComponent::restitution,"反発###Restitution",0,1);
            scalar(c,&SceneRuntime::RigidBodyComponent::gravityScale,"重力倍率###Gravity scale",-100000);
        });
        group(&P::rectTransform,"共通UI配置###Shared rect",[&](auto c){
            vector(c,&SceneRuntime::RectTransformComponent::position,"位置###Position");
            vector(c,&SceneRuntime::RectTransformComponent::size,"サイズ###Size",0);
            scalar(c,&SceneRuntime::RectTransformComponent::rotation,"回転（ラジアン）###Rotation",-100000);
        });
        ImGui::TextWrapped("全対象にあるコンポーネントを表示します。値が異なる項目は混在表示になり、操作した項目だけを全対象へ反映します。");
        if(changed) state.RequestComponentBatch(std::move(objects),state.Interaction());
    }
private:
    template<class Component,class Value,class Widget>
    static bool Field(EditState& state,std::vector<SceneRuntime::ScenePlacement>& objects,
        std::optional<Component> SceneRuntime::ScenePlacement::*component,Value Component::*field,const char* label,Widget draw) {
        auto value=((objects.front().*component).value()).*field;
        const bool mixed=std::any_of(objects.begin(),objects.end(),[&](const auto& object){return ((object.*component).value()).*field!=value;});
        ImGui::PushItemFlag(ImGuiItemFlags_MixedValue,mixed);
        const bool changed=draw(value);ImGui::PopItemFlag();
        if(ImGui::IsItemActive() || ImGui::IsItemDeactivatedAfterEdit()) state.SetInteraction("multi/component/"+std::to_string(ImGui::GetItemID()));
        if(mixed && ImGui::IsItemHovered()) ImGui::SetTooltip("複数の値が混在しています：%s",label);
        if(mixed) {ImGui::SameLine();ImGui::TextDisabled("混在");}
        if(changed) Apply(objects,component,field,value);
        return changed;
    }
};
}
