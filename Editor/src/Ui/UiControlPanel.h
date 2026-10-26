#pragma once
#include "../Inspector/ComponentWidgets.h"
#include "../Editing/EditState.h"
#include "../Inspector/EnvironmentPanel.h"
#include <SceneRuntime/Scene/SceneLayout.h>
#include <imgui.h>
namespace Editor
{
    /// <summary>自動レイアウトの方向・寸法・余白を編集します。</summary>
    void DrawLayoutGroup(EditState& state, SceneRuntime::LayoutGroupComponent& c);
    /// <summary>UI文字列の編集とUndoの操作状態を記録します。</summary>
    void DrawControlText(EditState& state, const char* label, std::string& value);
    /// <summary>UIコンポーネント共通の有効化・リセット・削除を表示します。</summary>
    template <class Component, class Draw> inline void DrawUiControl(Component& optional, const char* title, Draw draw)
    {
        if (!optional || !ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen))
            return;
        ImGui::PushID(optional->id.c_str());
        ImGui::Checkbox("有効###Enabled", &optional->enabled);
        draw(*optional);
        if (ImGui::Button("リセット###Reset"))
        {
            const auto id = optional->id;
            optional = std::decay_t<decltype(*optional)>{};
            optional->id = id;
        }
        ImGui::SameLine();
        if (ImGui::Button("削除###Remove"))
            optional.reset();
        ImGui::PopID();
    }
    /// <summary>汎用UIのプロパティーを通常のコンポーネント編集とUndoへ接続します。</summary>
    void DrawUiControls(EditState& state, SceneRuntime::ScenePlacement& p);
    /// <summary>汎用UIを追加し、必要なRectTransformも作成します。</summary>
    bool AddUiControls(SceneRuntime::ScenePlacement& p);
}
