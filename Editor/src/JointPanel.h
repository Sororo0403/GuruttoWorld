#pragma once
#include "EditState.h"
#include <imgui.h>
#include <cstring>
#include <functional>

namespace Editor
{
    /// <summary>種類に応じた距離または角度制限を編集します。</summary>
    inline bool DrawJointLimits(SceneRuntime::JointComponent& joint,const std::function<void()>& track)
    {
        bool edited=false;
        if(joint.type=="distance")
        {
            edited=ImGui::DragFloat("最小距離###Minimum distance",&joint.minDistance,0.01f,0,joint.maxDistance,"%.3f",ImGuiSliderFlags_AlwaysClamp);track();
            edited=ImGui::DragFloat("最大距離###Maximum distance",&joint.maxDistance,0.01f,joint.minDistance,100000,"%.3f",ImGuiSliderFlags_AlwaysClamp) || edited;track();
        }
        else if(joint.type!="fixed")
        {
            edited=ImGui::DragFloat3("ローカル軸###Local axis",joint.axis.data(),0.01f,-1,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);track();
            edited=ImGui::SliderFloat("最小角度###Minimum angle",&joint.minimum,-180,0) || edited;track();
            edited=ImGui::SliderFloat("最大角度###Maximum angle",&joint.maximum,0,180) || edited;track();
            if(joint.type=="swingTwist") {edited=ImGui::SliderFloat("Swing角度###Swing",&joint.swing,0,180) || edited;track();}
        }
        return edited;
    }
    /// <summary>剛体接続を編集しUndo対象の操作を追跡します。</summary>
    inline bool DrawJoint(EditState& state,SceneRuntime::ScenePlacement& candidate)
    {
        if(!candidate.joint || !ImGui::CollapsingHeader("剛体接続###Joint",ImGuiTreeNodeFlags_DefaultOpen)) return false;
        auto& joint=*candidate.joint;ImGui::PushID(joint.id.c_str());bool edited=false;
        const auto track=[&]{if(ImGui::IsItemActive() || ImGui::IsItemDeactivatedAfterEdit()) state.SetInteraction("joint/"+std::to_string(ImGui::GetItemID()));};
        edited=ImGui::Checkbox("有効###Enabled",&joint.enabled) || edited;
        const char* kinds[]={"fixed","hinge","distance","swingTwist"};const char* labels[]={"固定","ヒンジ","距離","Swing/Twist"};
        int kind=0;for(int index=0;index<4;++index) if(joint.type==kinds[index]) kind=index;
        if(ImGui::Combo("種類###Kind",&kind,labels,4)) {joint.type=kinds[kind];edited=true;}
        std::array<char,256> target{};std::copy_n(joint.target.begin(),std::min(joint.target.size(),target.size()-1),target.begin());
        if(ImGui::InputText("接続先ID（空はWorld）###Target",target.data(),target.size())) {joint.target=target.data();edited=true;}track();
        edited=ImGui::Checkbox("接続先と衝突###Collide connected",&joint.collideConnected) || edited;
        edited=ImGui::DragFloat3("自身のアンカー###Anchor",joint.anchor.data(),0.01f,-100000,100000,"%.3f",ImGuiSliderFlags_AlwaysClamp) || edited;track();
        edited=ImGui::DragFloat3("接続先のアンカー###Connected anchor",joint.connectedAnchor.data(),0.01f,-100000,100000,"%.3f",ImGuiSliderFlags_AlwaysClamp) || edited;track();
        edited=DrawJointLimits(joint,track) || edited;
        if(ImGui::Button("削除###Remove joint")) {candidate.joint.reset();edited=true;}
        ImGui::PopID();return edited;
    }
}
