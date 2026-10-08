#pragma once
#include <SceneRuntime/Animator.h>
#include <imgui.h>
#include <algorithm>

namespace Editor
{
    class AnimationEventPanel final
    {
        /// <summary>イベント名と文字列引数を編集します。</summary>
        static bool Text(const char* label,std::string& value)
        {
            std::vector<char> buffer(std::max(size_t(2048),value.size()+1)); std::copy(value.begin(),value.end(),buffer.begin());
            if (!ImGui::InputText(label,buffer.data(),buffer.size())) return false; value=buffer.data(); return true;
        }
    public:
        /// <summary>クリップ時刻・イベント名・各引数・最低ブレンド重みを編集します。</summary>
        static bool Draw(SceneRuntime::AnimatorComponent& animator,const Engine::SkeletonData* rig)
        {
            bool edited=false; ImGui::PushID("animationEvents");
            if (ImGui::TreeNode("events","アニメーションイベント (%zu)",animator.events.size()))
            {
                ImGui::TextWrapped("指定時刻を通過すると、この個体のScriptへ次の更新で通知します。ループごとに再通知し、同じクリップを混ぜた重複は除きます。");
                for (size_t index=0;index<animator.events.size();++index)
                {
                    auto& key=animator.events[index]; ImGui::PushID(static_cast<int>(index));
                    if (ImGui::BeginCombo("clip###Clip",key.clip.c_str()))
                    {
                        if (rig) for (const auto& clip : rig->clips) if (ImGui::Selectable(clip.name.c_str(),key.clip==clip.name)) { key.clip=clip.name; key.time=std::min(key.time,clip.duration); edited=true; }
                        ImGui::EndCombo();
                    }
                    float end=1000000; if (rig) for (const auto& clip : rig->clips) if (clip.name==key.clip) end=std::min(end,clip.duration);
                    edited=ImGui::DragFloat("時刻（秒）###Time",&key.time,.01f,0,end,"%.4f",ImGuiSliderFlags_AlwaysClamp) || edited;
                    edited=Text("イベント名###Name",key.name) || edited;
                    edited=ImGui::DragFloat("数値引数###Value",&key.value,.01f,-1000000,1000000,"%.3f",ImGuiSliderFlags_AlwaysClamp) || edited;
                    edited=Text("文字列引数###String",key.stringValue) || edited;
                    edited=ImGui::InputInt("整数引数###Integer",&key.intValue) || edited;
                    edited=ImGui::SliderFloat("最低重み###Weight",&key.minimumWeight,0,1) || edited;
                    if (ImGui::Button("イベントを削除###Remove")) { animator.events.erase(animator.events.begin()+index); edited=true; ImGui::PopID(); break; }
                    ImGui::Separator(); ImGui::PopID();
                }
                if (animator.events.size()<256 && rig && !rig->clips.empty() && ImGui::Button("イベントを追加###Add"))
                { SceneRuntime::AnimatorEventKey key; key.clip=rig->clips.front().name; key.name="event"+std::to_string(animator.events.size()+1); animator.events.push_back(std::move(key)); edited=true; }
                if (animator.events.size()>1 && ImGui::Button("クリップ・時刻順に並べる###Sort"))
                { std::ranges::stable_sort(animator.events,[](const auto& a,const auto& b) { return a.clip==b.clip ? a.time<b.time : a.clip<b.clip; }); edited=true; }
                ImGui::TreePop();
            }
            ImGui::PopID(); return edited;
        }
    };
}
