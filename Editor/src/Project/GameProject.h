#pragma once
#include <Engine/Assets/AssetDatabase.h>
#include <Engine/Core/Log.h>
#include <SceneRuntime/Project/ProjectSettings.h>
#include <algorithm>
#include <array>
#include <optional>
#include <commdlg.h>
#include <shellapi.h>
#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "Shell32.lib")

namespace Editor
{
    struct GameProject final
    {
        static bool ValidName(const std::string& name)
        {
            return Engine::ProjectContext::ValidName(name);
        }
        /// <summary>外部ゲームプロジェクトを検証し、コンテンツのルートを取得します。</summary>
        static std::filesystem::path Open(std::filesystem::path path);

        /// <summary>プロジェクトの宣言ファイルを選択し、キャンセル時は未設定を返します。</summary>
        static std::optional<std::filesystem::path> Choose();
        /// <summary>選択したプロジェクトを別のエディターウィンドウで開きます。</summary>
        static void BrowseAndOpen();
    };
}
