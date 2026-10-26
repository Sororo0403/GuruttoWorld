#pragma once
#include <SceneRuntime/Scripts/ScriptSchema.h>
#include <algorithm>
#include <Engine/Assets/AssetDatabase.h>
#include <cctype>
#include <filesystem>
#include <set>
#include <map>
#include <string>
#include <vector>
#include <optional>
#include <initializer_list>
#include <string_view>

namespace Editor
{
    enum class AssetKind
    {
        Model,
        Scene,
        Texture,
        Audio,
        Shader,
        Font,
        Prefab,
        Material
    };
    struct ProjectAsset
    {
        std::filesystem::path path;
        AssetKind kind = AssetKind::Model;
    };
    class ProjectCatalog final
    {
    public:
        static std::string Text(const std::filesystem::path& path)
        {
            const auto utf8 = path.generic_u8string();
            return {utf8.begin(), utf8.end()};
        }
        /// <summary>素材とメタデータを走査します。</summary>
        bool Scan(const std::filesystem::path& root);
        /// <summary>対象の種類を判定します。</summary>
        static std::optional<AssetKind> Kind(const std::filesystem::path& path);
        /// <summary>現在の状態の表示名を返します。</summary>
        static const char* Label(AssetKind kind);
        /// <summary>指定した条件との一致を確認します。</summary>
        static bool Matches(const ProjectAsset& asset, const std::filesystem::path& folder, const std::string& search);
        /// <summary>DLLのロード状態に依存しない宣言済みスクリプトのスキーマを取得します。</summary>
        const std::map<std::string, SceneRuntime::ScriptSchema>& ScriptSchemas() const
        {
            return schemas_;
        }
        const std::filesystem::path& Root() const
        {
            return root_;
        }
        /// <summary>依存ファイルの再走査を、派生したプレビューの更新へ伝える世代を返します。</summary>
        size_t Revision() const
        {
            return revision_;
        }
        const std::vector<ProjectAsset>& Assets() const
        {
            return assets_;
        }
        const std::vector<std::filesystem::path>& Folders() const
        {
            return folders_;
        }
        /// <summary>子要素を処理します。</summary>
        const std::vector<std::filesystem::path>& Children(const std::filesystem::path& folder) const;
        const std::string& Error() const
        {
            return error_;
        }

    private:
        size_t revision_ = 0;
        static bool HasExtension(const std::string& extension, std::initializer_list<std::string_view> supported)
        {
            return std::find(supported.begin(), supported.end(), extension) != supported.end();
        }
        /// <summary>指定フォルダー内の素材を走査します。</summary>
        static void ScanFolder(const std::filesystem::path& root, const std::filesystem::path& folder,
                               std::vector<ProjectAsset>& assets, std::set<std::filesystem::path>& folders);
        /// <summary>文字列を小文字へ変換します。</summary>
        static std::string Lower(std::string value);
        std::map<std::string, SceneRuntime::ScriptSchema> schemas_;
        std::filesystem::path root_;
        std::vector<ProjectAsset> assets_;
        std::vector<std::filesystem::path> folders_;
        std::map<std::filesystem::path, std::vector<std::filesystem::path>> children_;
        std::string error_;
    };
}
