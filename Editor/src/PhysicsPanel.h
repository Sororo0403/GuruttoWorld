#pragma once
#include "EditState.h"
#include "ProjectCatalog.h"
#include "JointPanel.h"
#include <imgui.h>

namespace Editor
{
    inline bool DrawPhysics(EditState& state,SceneRuntime::ScenePlacement& candidate,const ProjectCatalog* catalog)
    {
        bool edited=false;
        const auto track=[&] { if (ImGui::IsItemActive() || ImGui::IsItemDeactivatedAfterEdit()) state.SetInteraction("component/"+std::to_string(ImGui::GetItemID())); };
        if (candidate.boxCollider && ImGui::CollapsingHeader("衝突判定###Collider",ImGuiTreeNodeFlags_DefaultOpen))
        {
            auto& c=*candidate.boxCollider; ImGui::PushID(c.id.c_str());
            edited=ImGui::Checkbox("有効###Enabled",&c.enabled) || edited;
            const char* shapes[]={"box","sphere","capsule","mesh","terrain","tilemap"}; const char* labels[]={"箱","球","カプセル","メッシュ","地形","タイルマップ"};
            int shape=0; for (int i=0;i<6;++i) if (c.shape==shapes[i]) shape=i;
            if (ImGui::Combo("形状###Shape",&shape,labels,6)) { c.shape=shapes[shape]; edited=true; }
            edited=ImGui::DragFloat3("中心###Center",c.center.data(),0.05f,-100000,100000,"%.3f",ImGuiSliderFlags_AlwaysClamp) || edited; track();
            if (c.shape=="box") { edited=ImGui::DragFloat3("大きさ###Size",c.size.data(),0.05f,0.001f,100000,"%.3f",ImGuiSliderFlags_AlwaysClamp) || edited; track(); }
            if (c.shape=="sphere" || c.shape=="capsule") { edited=ImGui::DragFloat("半径###Radius",&c.radius,0.05f,0.001f,100000,"%.3f",ImGuiSliderFlags_AlwaysClamp) || edited; track(); }
            if (c.shape=="capsule") { edited=ImGui::DragFloat("円柱部の半高さ###Half height",&c.halfHeight,0.05f,0,100000,"%.3f",ImGuiSliderFlags_AlwaysClamp) || edited; track(); }
            if (c.shape=="mesh")
            {
                if (ImGui::BeginCombo("形状モデル###Collision model",c.model.empty() ? "描画モデルを使用" : ProjectCatalog::Text(c.model.filename()).c_str()))
                {
                    if (ImGui::Selectable("描画モデルを使用",c.model.empty())) { c.model.clear(); edited=true; }
                    if (catalog) for (const auto& asset : catalog->Assets()) if (asset.kind==AssetKind::Model)
                        if (ImGui::Selectable(ProjectCatalog::Text(asset.path).c_str(),asset.path==c.model)) { c.model=asset.path; edited=true; }
                    ImGui::EndCombo();
                }
                edited=ImGui::Checkbox("凸形状に変換（動的剛体用）###Convex",&c.convex) || edited;
                ImGui::TextWrapped("凹メッシュは静止物体または移動床、凸形状は動的剛体にも使用できます。");
            }
            edited=ImGui::Checkbox("Trigger（通り抜ける）###Trigger",&c.isTrigger) || edited;
            int layer=static_cast<int>(c.layer);
            if (ImGui::SliderInt("レイヤー###Layer",&layer,0,31)) { c.layer=static_cast<unsigned int>(layer); edited=true; } track();
            edited=ImGui::InputScalar("衝突マスク（16進）###Mask",ImGuiDataType_U32,&c.mask,nullptr,nullptr,"%08X",ImGuiInputTextFlags_CharsHexadecimal) || edited; track();
            if (ImGui::Button("リセット###Reset Collider")) { const auto id=c.id; c=SceneRuntime::BoxColliderComponent{}; c.id=id; edited=true; }
            ImGui::SameLine(); if (ImGui::Button("削除###Remove Collider")) { candidate.boxCollider.reset(); edited=true; }
            ImGui::PopID();
        }
        if (candidate.rigidBody && ImGui::CollapsingHeader("剛体###RigidBody",ImGuiTreeNodeFlags_DefaultOpen))
        {
            auto& body=*candidate.rigidBody; ImGui::PushID(body.id.c_str());
            edited=ImGui::Checkbox("有効###Enabled",&body.enabled) || edited;
            int motion=body.motion=="kinematic" ? 1 : 0; const char* modes[]={"動的（物理で移動）","Kinematic（Scriptや演出で移動）"};
            if (ImGui::Combo("移動方式###Motion",&motion,modes,2)) { body.motion=motion==0 ? "dynamic" : "kinematic"; edited=true; }
            edited=ImGui::Checkbox("連続衝突判定###Continuous",&body.continuous) || edited;
            edited=ImGui::Checkbox("2D（XY平面・Z軸回転）###Planar physics",&body.planar) || edited;
            edited=ImGui::DragFloat("質量###Mass",&body.mass,0.05f,0.001f,100000,"%.3f",ImGuiSliderFlags_AlwaysClamp) || edited; track();
            for (const auto& field : {std::pair{"摩擦###Friction",&body.friction},std::pair{"反発###Restitution",&body.restitution}})
            { edited=ImGui::SliderFloat(field.first,field.second,0,1) || edited; track(); }
            for (const auto& field : {std::pair{"重力倍率###Gravity scale",&body.gravityScale},std::pair{"移動の減衰###Linear damping",&body.linearDamping},std::pair{"回転の減衰###Angular damping",&body.angularDamping}})
            { edited=ImGui::DragFloat(field.first,field.second,0.01f,0,10,"%.3f",ImGuiSliderFlags_AlwaysClamp) || edited; track(); }
            edited=ImGui::DragFloat3("初期速度###Velocity",body.velocity.data(),0.1f,-100000,100000,"%.3f",ImGuiSliderFlags_AlwaysClamp) || edited; track();
            edited=ImGui::DragFloat3("初期角速度（rad/秒）###Angular velocity",body.angularVelocity.data(),0.1f,-100000,100000,"%.3f",ImGuiSliderFlags_AlwaysClamp) || edited; track();
            if (!candidate.boxCollider || !candidate.boxCollider->enabled) ImGui::TextWrapped("剛体には有効な衝突判定が必要です。");
            if (ImGui::Button("削除###Remove RigidBody")) { candidate.rigidBody.reset(); edited=true; }
            ImGui::PopID();
        }
        return DrawJoint(state,candidate) || edited;
    }
}
