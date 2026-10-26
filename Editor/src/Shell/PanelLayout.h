#pragma once
#include <imgui.h>
#include <imgui_internal.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include "EditorLanguage.h"
#include <Engine/Platform/Window.h>

namespace Editor::PanelLayout
{
    enum class Panel
    {
        Commands,
        Objects,
        Inspector,
        Gizmo,
        Models,
        Camera,
        Scene
    };
    inline bool requested = false;
    inline std::filesystem::path settingsPath;
    inline std::string error;
    inline void Reset()
    {
        requested = true;
    }
    /// <summary>初期化します。</summary>
    void Initialize(const std::filesystem::path& path);
    /// <summary>現在の内容を保存します。</summary>
    bool Save();
    /// <summary>既定のパネル配置を構築します。</summary>
    void BuildDefault(ImGuiID root);
    /// <summary>現在のフレームの表示を準備します。</summary>
    void BeginFrame(bool preview = false);
    /// <summary>既存のパネル呼び出しとの互換用です。個別の配置は行わず、ドックスペースと保存設定に委ねます。</summary>
    inline void Place(Panel) {}
}
