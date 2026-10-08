#pragma once
#include <SceneRuntime/Animator.h>
#include <imgui.h>
#include <algorithm>
namespace Editor
{
    class IkPanel final
    {
        static bool Bone(const char* label,std::string& name,const Engine::SkeletonData* rig)
        {
            bool changed=false;
            if (ImGui::BeginCombo(label,name.c_str()))
            {
                if (rig) for (const auto& node : rig->nodes) if (ImGui::Selectable(node.name.c_str(),name==node.name)) { name=node.name; changed=true; }
                ImGui::EndCombo();
            }
            return changed;
        }
    public:
        static bool Draw(SceneRuntime::AnimatorComponent& animator,const Engine::SkeletonData* rig)
        {
            bool changed=false; ImGui::PushID("ikConstraints");
            if (ImGui::TreeNode("ik","二関節IK (%zu)",animator.ik.size()))
            {
                ImGui::TextWrapped("目標・補助点はSkeleton座標、またはWorld座標で指定します。切り替え時は入力値を選択した座標として解釈します。直接つながる3ボーンと正の一様Scaleが必要です。制約は上から順に適用します。");
                for (size_t index=0;index<animator.ik.size();++index)
                {
                    auto& item=animator.ik[index]; ImGui::PushID(static_cast<int>(index));
                    std::vector<char> name(std::max(size_t(512),item.name.size()+1)); std::copy(item.name.begin(),item.name.end(),name.begin());
                    if (ImGui::InputText("名前###Name",name.data(),name.size())) { item.name=name.data(); changed=true; }
                    changed=ImGui::Checkbox("有効###Enabled",&item.enabled) || changed;
                    changed=ImGui::Checkbox("World座標###WorldSpace",&item.worldSpace) || changed;
                    changed=Bone("根元###Root",item.root,rig) || changed;
                    changed=Bone("中間###Middle",item.middle,rig) || changed;
                    changed=Bone("先端###Tip",item.tip,rig) || changed;
                    changed=ImGui::DragFloat3("目標###Target",item.target.data(),.01f,-1000000,1000000,"%.3f",ImGuiSliderFlags_AlwaysClamp) || changed;
                    changed=ImGui::DragFloat3("曲げ方向の補助点###Hint",item.hint.data(),.01f,-1000000,1000000,"%.3f",ImGuiSliderFlags_AlwaysClamp) || changed;
                    changed=ImGui::SliderFloat("重み###Weight",&item.weight,0,1) || changed;
                    if (ImGui::Button("削除###Remove")) { animator.ik.erase(animator.ik.begin()+index); changed=true; ImGui::PopID(); break; }
                    ImGui::Separator(); ImGui::PopID();
                }
                if (rig && animator.ik.size()<16 && ImGui::Button("IKを追加###Add"))
                {
                    for (size_t tip=0;tip<rig->nodes.size();++tip)
                    {
                        const int middle=rig->nodes[tip].parent;
                        if (middle<0) continue; const int root=rig->nodes[static_cast<size_t>(middle)].parent; if (root<0) continue;
                        SceneRuntime::AnimatorIkConstraint item; size_t suffix=1;
                        do { item.name="IK"+std::to_string(suffix++); } while (std::ranges::any_of(animator.ik,[&](const auto& other) { return other.name==item.name; }));
                        item.root=rig->nodes[static_cast<size_t>(root)].name; item.middle=rig->nodes[static_cast<size_t>(middle)].name; item.tip=rig->nodes[tip].name;
                        const auto matrices=Engine::Skeleton::Matrices(*rig,Engine::Skeleton::Sample(*rig,"",0,false));
                        const auto& tipMatrix=matrices[tip]; const auto& middleMatrix=matrices[static_cast<size_t>(middle)];
                        item.target={tipMatrix._41,tipMatrix._42,tipMatrix._43}; item.hint={middleMatrix._41,middleMatrix._42,middleMatrix._43+1};
                        animator.ik.push_back(std::move(item)); changed=true; break;
                    }
                }
                ImGui::TreePop();
            }
            ImGui::PopID(); return changed;
        }
    };
}
