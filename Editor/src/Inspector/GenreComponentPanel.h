#pragma once
#include "ComponentWidgets.h"
#include "../Editing/EditState.h"
#include "../Project/ProjectCatalog.h"
#include "EnvironmentPanel.h"
#include <SceneRuntime/Gameplay/Navigation.h>
#include <imgui.h>
#include <cmath>
namespace Editor::GenrePanel
{
    /// <summary>地形操作をUndoへまとめます。</summary>
    inline void Track(EditState& state)
    {
        Editor::TrackComponentInteraction(state);
    }
    /// <summary>グリッド解像度の変更で要素数を安全に更新します。</summary>
    template <class T, class Value>
    inline void Grid(EditState& state, T& c, std::vector<Value>& values, int minimum, Value fallback)
    {
        int size[]{static_cast<int>(c.columns), static_cast<int>(c.rows)};
        if (ImGui::DragInt2("グリッドサイズ###Grid dimensions", size, 1, minimum, 128, "%d",
                            ImGuiSliderFlags_AlwaysClamp))
        {
            c.columns = static_cast<unsigned int>(size[0]);
            c.rows = static_cast<unsigned int>(size[1]);
            values.assign(static_cast<size_t>(c.columns) * c.rows, fallback);
        }
        Track(state);
        ImGui::DragFloat("セルサイズ###Cell size", &c.cellSize, .05f, .001f, 1000, "%.3f",
                         ImGuiSliderFlags_AlwaysClamp);
        Track(state);
    }
    /// <summary>描画テクスチャをアセット一覧から選択します。</summary>
    void Texture(std::filesystem::path& path, const ProjectCatalog* catalog);
    /// <summary>Terrainの高さブラシと描画設定を編集します。</summary>
    void Terrain(EditState& state, SceneRuntime::ScenePlacement& p, const ProjectCatalog* catalog);
    /// <summary>Tilemapのセルをクリックで描き換えます。</summary>
    void TileGrid(SceneRuntime::TilemapComponent& c, int brush);
    /// <summary>Tilemapのアトラスとセル描画を編集します。</summary>
    void Tiles(EditState& state, SceneRuntime::ScenePlacement& p, const ProjectCatalog* catalog);
    /// <summary>NavMeshの焼き込みと通行可能セルを編集します。</summary>
    void NavMesh(EditState& state, SceneRuntime::ScenePlacement& p, const SceneRuntime::SceneLayout* scene);
    /// <summary>Agentの目的地と移動設定を編集します。</summary>
    void Agent(EditState& state, SceneRuntime::ScenePlacement& p, const SceneRuntime::SceneLayout* scene);
    /// <summary>ジャンル支援コンポーネントをインスペクターへ描画します。</summary>
    bool Draw(EditState& state, SceneRuntime::ScenePlacement& p, const ProjectCatalog* catalog,
              const SceneRuntime::SceneLayout* scene);
    /// <summary>ジャンル支援コンポーネント追加メニューを描画します。</summary>
    bool Add(SceneRuntime::ScenePlacement& p);
}
