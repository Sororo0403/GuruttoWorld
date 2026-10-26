#pragma once
#include "../Editing/EditState.h"
#include "../Project/ProjectCatalog.h"
#include <SceneRuntime/Gameplay/MenuConfiguration.h>
#include <imgui.h>
#include <algorithm>

namespace Editor
{
    struct MenuWidgets
    {
        EditState& state;
        /// <summary>文字列の編集項目を表示します。</summary>
        void string(const char* label, std::string& value);
        /// <summary>数値の編集項目を表示します。</summary>
        void number(const char* label, float& value, float low = -100000, float high = 100000);
        /// <summary>選択肢を表示して値を編集します。</summary>
        static void choice(const char* label, std::string& value, std::initializer_list<const char*> options);
    };
    /// <summary>メニューの遷移先の編集画面を描画します。</summary>
    void DrawMenuSceneTarget(SceneRuntime::MenuEntry& e, const ProjectCatalog* catalog);
    /// <summary>メニュー項目の編集画面を描画します。</summary>
    void DrawMenuEntries(MenuWidgets& widgets, SceneRuntime::MenuConfiguration& c, const ProjectCatalog* catalog);

    /// <summary>メニューの設定項目の編集画面を描画します。</summary>
    void DrawMenuSettings(MenuWidgets& widgets, SceneRuntime::MenuConfiguration& c);

    /// <summary>メニュー入力の編集画面を描画します。</summary>
    void DrawMenuInputs(MenuWidgets& widgets, SceneRuntime::MenuConfiguration& c);

    /// <summary>メニューの状態割り当ての編集画面を描画します。</summary>
    void DrawMenuBindings(MenuWidgets& widgets, SceneRuntime::MenuConfiguration& c);
    /// <summary>メニュー設定の編集画面を描画します。</summary>
    void DrawMenuConfiguration(EditState& state, SceneRuntime::CanvasComponent& canvas, const ProjectCatalog* catalog);
}
