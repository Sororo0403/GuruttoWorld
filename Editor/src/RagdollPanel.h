#pragma once
#include "EditState.h"
#include <imgui.h>
namespace Editor
{
    /// <summary>ラグドールの生成と物理・アニメーション合成をInspectorで編集します。</summary>
    inline bool DrawRagdoll(EditState& state,SceneRuntime::ScenePlacement& object)
    {
        if(!object.ragdoll || !ImGui::CollapsingHeader("ラグドール###Ragdoll",ImGuiTreeNodeFlags_DefaultOpen)) return false;
        auto& c=*object.ragdoll; ImGui::PushID(c.id.c_str()); bool edited=ImGui::Checkbox("有効###Enabled",&c.enabled);
        edited=ImGui::Checkbox("物理制御を有効化###Active physics",&c.active) || edited;
        edited=ImGui::SliderFloat("物理姿勢の合成重み###Pose weight",&c.weight,0,1) || edited;
        edited=ImGui::DragFloat("生成時の剛体質量###Generated body mass",&c.bodyMass,.1f,.01f,10000,"%.2f",ImGuiSliderFlags_AlwaysClamp) || edited;
        edited=ImGui::DragFloat("生成時の骨の太さ###Generated bone radius",&c.radius,.005f,.005f,100,"%.3f",ImGuiSliderFlags_AlwaysClamp) || edited;
        edited=ImGui::SliderFloat("生成時のSwing制限###Generated swing limit",&c.swing,0,180) || edited;
        edited=ImGui::SliderFloat("生成時のTwist制限###Generated twist limit",&c.twist,0,180) || edited;
        ImGui::BeginDisabled(edited || !c.bones.empty() || !object.animator || !object.meshRenderer);
        if(ImGui::Button("現在の姿勢から剛体とJointを生成###Generate ragdoll")) {ObjectRequest request; request.action=ObjectAction::GenerateRagdoll; request.id=object.id; state.Request(std::move(request));}
        ImGui::EndDisabled();
        ImGui::TextWrapped("生成した骨オブジェクトはHierarchyで編集できます。無効時はアニメーションへ追従するkinematic剛体、有効時はJointでつながるdynamic剛体になります。生成後のCollider・Joint調整は各骨で行います。");
        for(const auto& bone:c.bones) ImGui::Text("%s -> %s",bone.bone.c_str(),bone.body.c_str());
        ImGui::PopID(); return edited;
    }
}
