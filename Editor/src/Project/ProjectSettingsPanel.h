#pragma once
#include "ProjectCatalog.h"
#include "InputBindingsPanel.h"
#include <SceneRuntime/Project/ProjectSettings.h>
#include <imgui.h>

namespace Editor
{
    class ProjectSettingsPanel
    {
    public:
        /// <summary>保存完了の通知を取得して消去します。</summary>
        bool TakeSaved();
        /// <summary>指定した対象を開きます。</summary>
        void Open(const std::filesystem::path& root);
        /// <summary>編集画面を描画します。</summary>
        void Draw(const std::filesystem::path& root, const ProjectCatalog& catalog);

    private:
        SceneRuntime::ProjectSettings settings_;
        std::array<char, 1025> title_{};
        std::string error_;
        bool requested_ = false, saved_ = false;
        bool loaded_ = false;
    };
}
