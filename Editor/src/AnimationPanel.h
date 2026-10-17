#pragma once
#include "EditState.h"
#include "AnimationTimeline.h"
#include <imgui.h>

namespace Editor
{
    class AnimationPanel final
    {
    public:
        /// <summary>対象オブジェクトのキーフレーム・時計・補間を編集します。</summary>
        static bool Draw(EditState& state, SceneRuntime::ScenePlacement& placement)
        {
            if (!placement.animation ||
                !ImGui::CollapsingHeader("アニメーション###Animation", ImGuiTreeNodeFlags_DefaultOpen))
                return false;
            auto& animation = *placement.animation;
            ImGui::PushID(animation.id.c_str());
            bool edited = ImGui::Checkbox("有効###Enabled", &animation.enabled);
            ImGui::TextWrapped(
                "登場・待機・開始の時計に合わせてキーフレームを再生します。位置は絶対値、回転はラジアンです。");
            for (size_t index = 0; index < animation.tracks.size(); ++index)
            {
                ImGui::PushID(static_cast<int>(index));
                auto& track = animation.tracks[index];
                if (ImGui::TreeNode("track", "トラック %zu / %s", index + 1, track.property.c_str()))
                {
                    edited |= DrawTrack(state, track);
                    if (ImGui::Button("トラックを削除###Delete track"))
                    {
                        animation.tracks.erase(animation.tracks.begin() + index);
                        edited = true;
                        ImGui::TreePop();
                        ImGui::PopID();
                        break;
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            if (animation.tracks.size() < 32 && ImGui::Button("トラックを追加###Add track"))
            {
                SceneRuntime::AnimationTrack track;
                if (placement.rectTransform)
                {
                    const auto& p = placement.rectTransform->position;
                    track.keys = {{0, {p[0], p[1], 0, 0}}, {1, {p[0], p[1], 0, 0}}};
                }
                else
                {
                    track.property = "position";
                    const auto& p = placement.position;
                    track.keys = {{0, {p[0], p[1], p[2], 0}}, {1, {p[0], p[1], p[2], 0}}};
                }
                animation.tracks.push_back(std::move(track));
                edited = true;
            }
            if (ImGui::Button("アニメーションを削除###Remove Animation"))
            {
                placement.animation.reset();
                edited = true;
            }
            ImGui::PopID();
            return edited;
        }

    private:
        /// <summary>対象・時計・補間とキーフレームを編集します。</summary>
        static bool DrawTrack(EditState& state, SceneRuntime::AnimationTrack& track)
        {
            bool edited = false;
            if (ImGui::BeginCombo("対象###Property", track.property.c_str()))
            {
                for (const auto& property : SceneRuntime::Animation::Properties())
                    if (ImGui::Selectable(property.label, track.property == property.name))
                    {
                        track.property = property.name;
                        for (auto& key : track.keys)
                            for (unsigned int i = 0; i < property.channels; ++i)
                                key.value[i] = track.property == "scale"
                                                   ? 1.0f
                                                   : std::clamp(key.value[i], property.minimum, property.maximum);
                        edited = true;
                    }
                ImGui::EndCombo();
            }
            edited |= Choose("時計###Clock", track.clock, {"sceneTime", "motionTime", "startTime"},
                             {"登場・経過時間", "待機（背景演出OFFで停止）", "開始（決定後）"});
            std::array<char, 129> clock{};
            std::copy_n(track.clock.data(), std::min(track.clock.size(), clock.size() - 1), clock.data());
            if (ImGui::InputText("時計名（任意）###Custom clock", clock.data(), clock.size()))
            {
                track.clock = clock.data();
                edited = true;
            }
            Track(state);
            edited |= Choose("補間###Easing", track.easing, {"linear", "smooth", "outCubic", "outBack"},
                             {"等速", "滑らか", "素早く動いて減速", "少し行き過ぎて止まる"});
            edited |= ImGui::DragFloat("遅延（秒）###Delay", &track.delay, 0.01f, 0, 600, "%.2f",
                                       ImGuiSliderFlags_AlwaysClamp);
            Track(state);
            edited |= ImGui::Checkbox("ループ###Loop", &track.loop);
            edited |= AnimationTimeline::Draw(state, track);
            edited |= DrawKeys(state, track);
            if (!SceneRuntime::Animation::Valid(track))
                ImGui::TextUnformatted("時刻は昇順、サイズは0以上、不透明度は0〜1にしてください。");
            return edited;
        }
        /// <summary>キーを昇順の時刻と値で編集し、追加・削除します。</summary>
        static bool DrawKeys(EditState& state, SceneRuntime::AnimationTrack& track)
        {
            bool edited = false;
            for (size_t key = 0; key < track.keys.size(); ++key)
            {
                ImGui::PushID(static_cast<int>(key));
                auto& frame = track.keys[key];
                edited |= ImGui::DragFloat("時刻（秒）###Time", &frame.time, 0.01f, 0, 600, "%.2f",
                                           ImGuiSliderFlags_AlwaysClamp);
                Track(state);
                const auto properties = SceneRuntime::Animation::Properties();
                const auto info = std::find_if(properties.begin(), properties.end(),
                                               [&](const auto& p)
                                               {
                                                   return track.property == p.name;
                                               });
                if (info != properties.end())
                {
                    if (ImGui::DragFloat4("値（X / Y / Z / W）###Value", frame.value.data(), .1f, -1000000, 1000000,
                                          "%.3f", ImGuiSliderFlags_AlwaysClamp))
                    {
                        for (unsigned int i = 0; i < info->channels; ++i)
                            frame.value[i] = std::clamp(frame.value[i], info->minimum, info->maximum);
                        edited = true;
                    }
                }
                Track(state);
                if (track.keys.size() > 1 && ImGui::Button("キーを削除###Delete key"))
                {
                    track.keys.erase(track.keys.begin() + key);
                    edited = true;
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            if (track.keys.size() < 128 && ImGui::Button("キーを追加###Add key"))
            {
                auto frame = track.keys.empty() ? SceneRuntime::AnimationKey{} : track.keys.back();
                frame.time += 0.5f;
                track.keys.push_back(frame);
                edited = true;
            }
            return edited;
        }
        /// <summary>ドラッグ操作を一回のUndoへまとめます。</summary>
        static void Track(EditState& state)
        {
            if (ImGui::IsItemActive() || ImGui::IsItemDeactivatedAfterEdit())
                state.SetInteraction("animation/" + std::to_string(ImGui::GetItemID()));
        }
        /// <summary>保存用の値を日本語の選択肢で編集します。</summary>
        static bool Choose(const char* label, std::string& value, std::initializer_list<const char*> keys,
                           std::initializer_list<const char*> labels)
        {
            const char* preview = value.c_str();
            auto caption = labels.begin();
            for (const auto key : keys)
            {
                if (value == key)
                    preview = *caption;
                ++caption;
            }
            bool edited = false;
            if (ImGui::BeginCombo(label, preview))
            {
                caption = labels.begin();
                for (const auto key : keys)
                {
                    if (ImGui::Selectable(*caption, value == key))
                    {
                        value = key;
                        edited = true;
                    }
                    ++caption;
                }
                ImGui::EndCombo();
            }
            return edited;
        }
    };
}
