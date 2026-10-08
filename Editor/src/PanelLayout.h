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
    enum class Panel { Commands, Objects, Inspector, Gizmo, Models, Camera, Scene };
    inline bool requested = false;
    inline std::filesystem::path settingsPath;
    inline std::string error;
    inline void Reset() { requested = true; }
    inline void Initialize(const std::filesystem::path& path)
    {
        settingsPath = path;
        error.clear();
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        std::ifstream input(path, std::ios::binary);
        if (input)
        {
            const auto contents=Language::MigrateLayout(std::string((std::istreambuf_iterator<char>(input)), {}));
            ImGui::LoadIniSettingsFromMemory(contents.c_str(), contents.size());
        }
    }
    inline bool Save()
    {
        if (settingsPath.empty()) return false;
        try
        {
            std::filesystem::create_directories(settingsPath.parent_path());
            size_t size = 0;
            const char* contents = ImGui::SaveIniSettingsToMemory(&size);
            auto temporary=settingsPath; temporary+=".tmp";
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output.write(contents, static_cast<std::streamsize>(size));
            output.close();
            if (!output || !MoveFileExW(temporary.c_str(),settingsPath.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            { std::error_code ignored; std::filesystem::remove(temporary,ignored); throw std::runtime_error("エディターのパネル配置を保存できませんでした。"); }
            ImGui::GetIO().WantSaveIniSettings = false;
            error.clear();
            return true;
        }
        catch (const std::exception& failure) { error = failure.what(); return false; }
    }
    inline void BuildDefault(ImGuiID root)
    {
        ImGui::DockBuilderRemoveNode(root);
        ImGui::DockBuilderAddNode(root, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(root, ImGui::GetMainViewport()->WorkSize);
        ImGuiID center = root, left = 0, right = 0, bottom = 0, commands = 0;
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.23f, &left, &center);
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.30f, &right, &center);
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.28f, &bottom, &center);
        ImGui::DockBuilderSplitNode(left, ImGuiDir_Up, 0.25f, &commands, &left);
        ImGui::DockBuilderDockWindow("エディター###Street Editor", commands);
        ImGui::DockBuilderDockWindow("ヒエラルキー###Objects", left);
        ImGui::DockBuilderDockWindow("インスペクター###Inspector", right);
        ImGui::DockBuilderDockWindow("シーン###Scene", center);
        ImGui::DockBuilderDockWindow("ゲーム###Game", center);
        ImGui::DockBuilderDockWindow("UI編集###UI Editor", center);
        ImGui::DockBuilderDockWindow("プロジェクト###Models", bottom);
        ImGui::DockBuilderDockWindow("シーンカメラ###Debug Camera", bottom);
        ImGui::DockBuilderDockWindow("コンソール###Console", bottom);
        ImGui::DockBuilderFinish(root);
        if (auto* scene = ImGui::DockBuilderGetNode(center)) scene->SelectedTabId = ImHashStr("シーン###Scene");
    }
    inline void BeginFrame(bool preview = false)
    {
        const auto flags = preview ? ImGuiDockNodeFlags_KeepAliveOnly : ImGuiDockNodeFlags_None;
        const auto root = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), flags);
        const auto* node = ImGui::DockBuilderGetNode(root);
        if (!preview && (requested || !node || (!node->ChildNodes[0] && node->Windows.empty())))
        {
            BuildDefault(root);
            requested = false;
        }
        if (ImGui::GetIO().WantSaveIniSettings) Save();
    }
    inline void Place(Panel) {} // Window placement now belongs to the dockspace/settings.
}
