#pragma once
#include "../Project/ProjectCatalog.h"
#include "ComponentWidgets.h"
#include "EnvironmentPanel.h"
#include "../Assets/AudioPreview.h"
#include <imgui.h>
#include <cmath>
namespace Editor::AudioPanel
{
    /// <summary>音響フィールドの操作をUndoへまとめます。</summary>
    inline void Track(EditState& state)
    {
        Editor::TrackComponentInteraction(state);
    }
    /// <summary>音響名・状態キーを編集します。</summary>
    void String(EditState& state, const char* label, std::string& value);
    /// <summary>音響コンポーネント共通の有効・リセット・削除操作を描画します。</summary>
    template <class T, class Draw> inline void Component(std::optional<T>& component, const char* label, Draw draw)
    {
        if (!component || !ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen))
            return;
        ImGui::PushID(component->id.c_str());
        ImGui::Checkbox("有効###Enabled", &component->enabled);
        draw(*component);
        if (ImGui::Button("リセット###Reset"))
        {
            const auto id = component->id;
            component = T{};
            component->id = id;
        }
        ImGui::SameLine();
        if (ImGui::Button("削除###Remove"))
            component.reset();
        ImGui::PopID();
    }
    /// <summary>3D音源のクリップ・再生方式・ミキサー・減衰を編集します。</summary>
    void Source(EditState& state, SceneRuntime::AudioSourceComponent& c, const ProjectCatalog* catalog);
    /// <summary>ミキサーグループの実再生パラメーターを編集します。</summary>
    void Mixer(EditState& state, SceneRuntime::AudioMixerComponent& c);
    /// <summary>インスペクターの音響コンポーネントを描画します。</summary>
    void Draw(EditState& state, SceneRuntime::ScenePlacement& p, const ProjectCatalog* catalog);
    /// <summary>音響コンポーネント追加メニューを描画します。</summary>
    bool Add(SceneRuntime::ScenePlacement& p);
}
