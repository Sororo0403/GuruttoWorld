#pragma once
#include <SceneRuntime/Animator.h>
#include "ProjectCatalog.h"
#include "BlendTreePanel.h"
#include <imgui.h>
namespace Editor {
class AnimatorPanel final {
    static bool Text(const char* label,std::string& value) {
        std::vector<char> buffer(std::max(size_t(1024),value.size()+1));
        std::copy(value.begin(),value.end(),buffer.begin());
        if (!ImGui::InputText(label,buffer.data(),buffer.size())) return false;
        value=buffer.data(); return true;
    }
public:
    static bool Draw(SceneRuntime::ScenePlacement& object,const ProjectCatalog* catalog) {
        if (!object.animator || !ImGui::CollapsingHeader("骨格アニメーション###Animator")) return false;
        std::shared_ptr<const Engine::SkeletonData> rig;
        static std::filesystem::path cachedPath; static std::filesystem::file_time_type cachedTime;
        static std::shared_ptr<const Engine::SkeletonData> cachedRig;
        if (catalog && object.meshRenderer) {
            const auto path=catalog->Root()/object.Model(); std::error_code error;
            const auto time=std::filesystem::last_write_time(path,error);
            if (!error && (cachedPath!=path || cachedTime!=time)) {
                std::string message; cachedRig=Engine::Skeleton::Load(path,message); cachedPath=path; cachedTime=time;
            } rig=cachedRig;
        }
        auto& animator=*object.animator; ImGui::PushID(animator.id.c_str());
        bool edited=ImGui::Checkbox("有効###Enabled",&animator.enabled);
        edited=BlendTreePanel::Parameters(animator) || edited;
        if (ImGui::BeginCombo("初期状態###Initial state",animator.initialState.c_str())) {
            for (const auto& state : animator.states) if (ImGui::Selectable(state.name.c_str(),state.name==animator.initialState)) { animator.initialState=state.name; edited=true; }
            ImGui::EndCombo();
        }
        ImGui::TextWrapped("clipはglTFのアニメーション名です。空欄では初期姿勢を使用します。speed・grounded・入力Actionを遷移条件に使用できます。");
        for (size_t i=0;i<animator.states.size();++i) {
            ImGui::PushID(static_cast<int>(i)); auto& state=animator.states[i];
            if (ImGui::TreeNode("state","状態: %s",state.name.c_str())) {
                const auto oldName=state.name;
                if (Text("名前###Name",state.name)) {
                    if (animator.initialState==oldName) animator.initialState=state.name;
                    for (auto& transition : animator.transitions) { if (transition.from==oldName) transition.from=state.name; if (transition.to==oldName) transition.to=state.name; }
                    edited=true;
                }
                edited=BlendTreePanel::Motion("Motion###Clip",state.clip,state.blendTree,animator,rig.get()) || edited;
                edited=ImGui::DragFloat("再生速度###Speed",&state.speed,0.01f,0,1000,"%.2f",ImGuiSliderFlags_AlwaysClamp) || edited;
                edited=ImGui::Checkbox("ループ###Loop",&state.loop) || edited;
                if (animator.states.size()>1 && ImGui::Button("状態を削除###Remove")) {
                    const auto name=state.name;
                    animator.states.erase(animator.states.begin()+i);
                    if (animator.initialState==name) animator.initialState=animator.states.front().name;
                    std::erase_if(animator.transitions,[&](const auto& transition) { return transition.from==name || transition.to==name; }); edited=true;
                    ImGui::TreePop(); ImGui::PopID(); break;
                } ImGui::TreePop();
            } ImGui::PopID();
        }
        if (animator.states.size()<32 && ImGui::Button("状態を追加###Add state")) {
            SceneRuntime::AnimatorStateDefinition next; size_t number=animator.states.size()+1;
            do { next.name="State"+std::to_string(number++); } while (std::any_of(animator.states.begin(),animator.states.end(),[&](const auto& state) { return state.name==next.name; }));
            animator.states.push_back(next); edited=true;
        }
        edited=BlendTreePanel::Draw(animator,rig.get()) || edited;
        for (size_t i=0;i<animator.transitions.size();++i) {
            ImGui::PushID(static_cast<int>(i+100)); auto& transition=animator.transitions[i];
            if (ImGui::TreeNode("transition","遷移: %s -> %s",transition.from.c_str(),transition.to.c_str())) {
                for (const auto& field : {std::pair{"遷移元###From",&transition.from},std::pair{"遷移先###To",&transition.to}}) {
                    if (ImGui::BeginCombo(field.first,field.second->c_str())) {
                        if (field.second==&transition.from && ImGui::Selectable("*",transition.from=="*")) { transition.from="*"; edited=true; }
                        for (const auto& value : animator.states) if (ImGui::Selectable(value.name.c_str(),*field.second==value.name)) { *field.second=value.name; edited=true; }
                        ImGui::EndCombo();
                    }
                }
                edited=Text("パラメーター###Parameter",transition.parameter) || edited;
                if (ImGui::BeginCombo("比較###Comparison",transition.comparison.c_str())) {
                    for (const auto* comparison : {">",">=","<","<=","==","!="})
                        if (ImGui::Selectable(comparison,transition.comparison==comparison)) { transition.comparison=comparison; edited=true; }
                    ImGui::EndCombo();
                }
                edited=ImGui::InputFloat("閾値###Value",&transition.value) || edited;
                edited=ImGui::DragFloat("ブレンド秒###Blend",&transition.blendSeconds,0.01f,0,60,"%.2f",ImGuiSliderFlags_AlwaysClamp) || edited;
                edited=ImGui::DragFloat("終了時刻（-1で無効）###Exit",&transition.exitTime,0.01f,-1,100,"%.2f",ImGuiSliderFlags_AlwaysClamp) || edited;
                if (ImGui::Button("遷移を削除###Remove")) {
                    animator.transitions.erase(animator.transitions.begin()+i); edited=true;
                    ImGui::TreePop(); ImGui::PopID(); break;
                } ImGui::TreePop();
            } ImGui::PopID();
        }
        if (animator.transitions.size()<128 && ImGui::Button("遷移を追加###Add transition")) {
            SceneRuntime::AnimatorTransition next; next.from=animator.initialState; next.to=animator.initialState;
            animator.transitions.push_back(next); edited=true;
        }
        if (ImGui::Button("Animatorを削除###Remove Animator")) { object.animator.reset(); edited=true; }
        ImGui::PopID(); return edited;
    }
};
}
