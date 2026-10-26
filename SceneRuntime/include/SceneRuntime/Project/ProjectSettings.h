#pragma once
#include <Engine/Assets/AssetReferenceResolver.h>
#include <Engine/Core/Json.h>
#include <Engine/Core/FileReplacement.h>
#include <Engine/Assets/AssetDatabase.h>
#include <Engine/Input/InputActions.h>
#include <filesystem>
#include <fstream>
#include <Engine/Platform/Window.h>
#include <map>
#include <string_view>
#include <algorithm>

namespace SceneRuntime
{
    struct ProjectSettings
    {
        std::string title = "Game";
        std::string startupScene;
        bool importLegacySettings = false;
        std::map<std::string, std::string> sceneAliases;
        int width = 1280, height = 720;
        Engine::InputActions::Bindings inputActions = Engine::InputActions::Defaults();

        /// <summary>シーンの保存先パスを取得します。</summary>
        static bool ScenePath(const std::string& value);
        /// <summary>別名の件数・名前・シーンパスを検証します。</summary>
        void ValidateSceneAliases() const;
        /// <summary>設定の整合性を検証します。</summary>
        void Validate() const;
        /// <summary>プロジェクトで定義した別名、または明示的なシーンパスを解決します。</summary>
        std::string ResolveScene(std::string_view name) const;
        /// <summary>設定値と読み込み時の生データを保持し、後の保存で外部変更を検出します。</summary>
        static ProjectSettings Load(const std::filesystem::path& root);
        /// <summary>検証済み設定を保存し、読み込み元が外部で変更・削除された場合は保存を拒否します。</summary>
        void Save(const std::filesystem::path& root) const;

    private:
        mutable std::filesystem::path savedPath_;
        mutable std::optional<std::string> savedSource_;
        /// <summary>起動先と別名の参照を既存のアセット ID 管理へ渡し、改名後も同じシーンを追跡します。</summary>
        static void References(const std::filesystem::path& root, Engine::Json& json);
    };
}
