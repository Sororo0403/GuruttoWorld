#pragma once
#include "../Editing/EditState.h"
#include "AnimationTimeline.h"
#include <imgui.h>

namespace Editor
{
    class AnimationPanel final
    {
    public:
        /// <summary>対象オブジェクトのキーフレーム・時計・補間を編集します。</summary>
        static bool Draw(EditState& state, SceneRuntime::ScenePlacement& placement);

    private:
        /// <summary>対象・時計・補間とキーフレームを編集します。</summary>
        static bool DrawTrack(EditState& state, SceneRuntime::AnimationTrack& track);
        /// <summary>キーを昇順の時刻と値で編集し、追加・削除します。</summary>
        static bool DrawKeys(EditState& state, SceneRuntime::AnimationTrack& track);
        /// <summary>ドラッグ操作を一回のUndoへまとめます。</summary>
        static void Track(EditState& state)
        {
            if (ImGui::IsItemActive() || ImGui::IsItemDeactivatedAfterEdit())
                state.SetInteraction("animation/" + std::to_string(ImGui::GetItemID()));
        }
        /// <summary>保存用の値を日本語の選択肢で編集します。</summary>
        static bool Choose(const char* label, std::string& value, std::initializer_list<const char*> keys,
                           std::initializer_list<const char*> labels);
    };
}
