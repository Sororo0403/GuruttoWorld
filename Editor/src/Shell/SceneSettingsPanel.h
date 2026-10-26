#pragma once
#include "../Inspector/ComponentWidgets.h"
#include "../Editing/EditState.h"
#include "../Project/ProjectCatalog.h"
#include <imgui.h>
namespace Editor::SceneSettingsPanel
{
    inline void Track(EditState& state)
    {
        Editor::TrackComponentInteraction(state);
    }
    /// <summary>入力設定を編集します。</summary>
    bool Inputs(EditState& state, SceneRuntime::SceneSettings& settings);
    /// <summary>シミュレーション設定を編集します。</summary>
    bool Simulation(EditState& state, SceneRuntime::SceneSettings& settings);
    /// <summary>解像度の設定を編集します。</summary>
    bool Resolution(const char* label, unsigned int& value, unsigned int high);
    /// <summary>シェーダーの設定を編集します。</summary>
    bool Shader(const char* label, std::filesystem::path& value, const ProjectCatalog& catalog);
    /// <summary>描画設定を編集します。</summary>
    bool Rendering(EditState& state, SceneRuntime::SceneSettings& settings, const ProjectCatalog* catalog);
}
