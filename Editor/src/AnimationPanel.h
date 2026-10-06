#pragma once
#include "EditState.h"
#include <imgui.h>

namespace Editor
{
    class AnimationPanel final
    {
    public:
        /// <summary>対象オブジェクトのキーフレーム・時計・補間を編集します。</summary>
        static bool Draw(EditState& state, SceneRuntime::ScenePlacement& placement)
        {
            if (!placement.animation || !ImGui::CollapsingHeader("アニメーション###Animation",ImGuiTreeNodeFlags_DefaultOpen)) return false;
            auto& animation=*placement.animation;
            ImGui::PushID(animation.id.c_str());
            bool edited=ImGui::Checkbox("有効###Enabled",&animation.enabled);
            ImGui::TextWrapped("登場・待機・開始の時計に合わせてキーフレームを再生します。位置は絶対値、回転はラジアンです。");
            for (size_t index=0;index<animation.tracks.size();++index)
            {
                ImGui::PushID(static_cast<int>(index));
                auto& track=animation.tracks[index];
                if (ImGui::TreeNode("track","トラック %zu / %s",index+1,track.property.c_str()))
                {
                    edited=DrawTrack(state,track) || edited;
                    if (ImGui::Button("トラックを削除###Delete track"))
                    { animation.tracks.erase(animation.tracks.begin()+index); edited=true; ImGui::TreePop(); ImGui::PopID(); break; }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            if (animation.tracks.size()<32 && ImGui::Button("トラックを追加###Add track"))
            {
                SceneRuntime::AnimationTrack track;
                if (placement.rectTransform)
                {
                    const auto& p=placement.rectTransform->position;
                    track.keys={{0,{p[0],p[1],0,0}},{1,{p[0],p[1],0,0}}};
                }
                else
                {
                    track.property="position";
                    const auto& p=placement.position;
                    track.keys={{0,{p[0],p[1],p[2],0}},{1,{p[0],p[1],p[2],0}}};
                }
                animation.tracks.push_back(std::move(track)); edited=true;
            }
            if (ImGui::Button("アニメーションを削除###Remove Animation")) {placement.animation.reset(); edited=true;}
            ImGui::PopID(); return edited;
        }
    private:
        /// <summary>対象・時計・補間とキーフレームを編集します。</summary>
        static bool DrawTrack(EditState& state,SceneRuntime::AnimationTrack& track)
        {
            bool edited=Choose("対象###Property",track.property,
                {"position","rotation","uiPosition","uiSize","uiRotation","opacity"},
                {"3D位置","3D回転","UI位置","UIサイズ","UI回転","不透明度"});
            edited=Choose("時計###Clock",track.clock,{"sceneTime","motionTime","startTime"},
                {"登場・経過時間","待機（背景演出OFFで停止）","開始（決定後）"}) || edited;
            edited=Choose("補間###Easing",track.easing,{"linear","smooth","outCubic","outBack"},
                {"等速","滑らか","素早く動いて減速","少し行き過ぎて止まる"}) || edited;
            edited=ImGui::DragFloat("遅延（秒）###Delay",&track.delay,0.01f,0,600,"%.2f",ImGuiSliderFlags_AlwaysClamp) || edited; Track(state);
            edited=ImGui::Checkbox("ループ###Loop",&track.loop) || edited;
            edited=DrawKeys(state,track) || edited;
            if (!SceneRuntime::Animation::Valid(track)) ImGui::TextUnformatted("時刻は昇順、サイズは0以上、不透明度は0〜1にしてください。");
            return edited;
        }
        /// <summary>キーを昇順の時刻と値で編集し、追加・削除します。</summary>
        static bool DrawKeys(EditState& state,SceneRuntime::AnimationTrack& track)
        {
            bool edited=false;
            for (size_t key=0;key<track.keys.size();++key)
            {
                ImGui::PushID(static_cast<int>(key));
                auto& frame=track.keys[key];
                edited=ImGui::DragFloat("時刻（秒）###Time",&frame.time,0.01f,0,600,"%.2f",ImGuiSliderFlags_AlwaysClamp) || edited; Track(state);
                edited=ImGui::DragFloat4("値（X / Y / Z / W）###Value",frame.value.data(),0.1f,-100000,100000,"%.3f",ImGuiSliderFlags_AlwaysClamp) || edited; Track(state);
                if (track.keys.size()>1 && ImGui::Button("キーを削除###Delete key"))
                { track.keys.erase(track.keys.begin()+key); edited=true; ImGui::PopID(); break; }
                ImGui::PopID();
            }
            if (track.keys.size()<128 && ImGui::Button("キーを追加###Add key"))
            {
                auto frame=track.keys.empty() ? SceneRuntime::AnimationKey{} : track.keys.back();
                frame.time+=0.5f; track.keys.push_back(frame); edited=true;
            }
            return edited;
        }
        /// <summary>ドラッグ操作を一回のUndoへまとめます。</summary>
        static void Track(EditState& state)
        {
            if (ImGui::IsItemActive() || ImGui::IsItemDeactivatedAfterEdit())
                state.SetInteraction("animation/"+std::to_string(ImGui::GetItemID()));
        }
        /// <summary>保存用の値を日本語の選択肢で編集します。</summary>
        static bool Choose(const char* label,std::string& value,
            std::initializer_list<const char*> keys,std::initializer_list<const char*> labels)
        {
            const char* preview=value.c_str();
            auto caption=labels.begin();
            for (const auto key:keys) { if (value==key) preview=*caption; ++caption; }
            bool edited=false;
            if (ImGui::BeginCombo(label,preview))
            {
                caption=labels.begin();
                for (const auto key:keys)
                { if (ImGui::Selectable(*caption,value==key)) {value=key; edited=true;} ++caption; }
                ImGui::EndCombo();
            }
            return edited;
        }
    };
}
