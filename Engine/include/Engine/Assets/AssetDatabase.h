#pragma once
#include <Engine/Assets/AssetPath.h>
#include <Engine/Core/Validation.h>
#include <Engine/Core/FileContents.h>
#include <Engine/Core/FileReplacement.h>
#include <Engine/Assets/ContentContext.h>
#include <iterator>
#include <Engine/Core/Json.h>
#include <Engine/Assets/TextureImport.h>
#include <Engine/Assets/ContentFiles.h>
#include <filesystem>
#include <fstream>
#include <random>
#include <algorithm>
#include <map>
#include <vector>
#include <stdexcept>
#include <sstream>
#include <set>
#include <cmath>
#include <cctype>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace Engine
{
    struct AssetMetadata
    {
        std::string id;
        float scale = 1;
        bool flipV = false;
        std::vector<std::string> previousPaths;
        TextureImportSettings texture;
    };
    class AssetDatabase final
    {
        std::filesystem::path root_;
        struct Index
        {
            std::map<std::string, std::filesystem::path> ids;
            std::map<std::string, std::set<std::filesystem::path>> aliases;
            std::map<std::string, AssetMetadata> metadata;
        };
        /// <summary>同じ素材層の索引を共有し、変更通知がある場合だけ再構築します。</summary>
        static std::shared_ptr<const Index> AcquireIndex(const std::shared_ptr<const ContentContext>& context);
        /// <summary>検証済みの不変な索引を構築します。失敗した索引は公開しません。</summary>
        static std::shared_ptr<const Index> BuildIndex(const ContentContext& context);
        std::shared_ptr<const Index> index_;
        bool snapshot_ = false;
        std::shared_ptr<const ContentContext> context_;

    public:
        /// <summary>索引が使用する固定した素材層を取得します。</summary>
        const std::shared_ptr<const ContentContext>& Content() const
        {
            return context_;
        }
        /// <summary>論理アセットパスをUTF-8の保存値へ変換します。</summary>
        static std::string Text(const std::filesystem::path& path)
        {
            return AssetPath::Text(path);
        }

        /// <summary>UTF-8の保存値を論理パスへ変換します。</summary>
        static std::filesystem::path Path(const std::string& value)
        {
            return AssetPath::Path(value);
        }

        /// <summary>共通の論理パス規則でルート外への逸脱を拒否します。</summary>
        static bool Valid(const std::filesystem::path& path)
        {
            return AssetPath::Valid(path);
        }

        /// <summary>共通の保存形式でアセットIDを検証します。</summary>
        static bool ValidId(const std::string& id)
        {
            return AssetPath::ValidId(id);
        }

        static std::filesystem::path Sidecar(std::filesystem::path path)
        {
            path += ".meta";
            return path;
        }
        /// <summary>既存のメタデータ形式を、副作用なしに解釈して検証します。</summary>
        static AssetMetadata ParseMetadata(std::string_view source);
        /// <summary>素材と標準シェーダーのメタデータを読みます。存在しない場合だけ既定値を返します。</summary>
        static AssetMetadata Read(const std::filesystem::path& path);
        /// <summary>新しいアセットIDと既定のインポート設定を、ファイルを変更せず作成します。</summary>
        static AssetMetadata CreateMetadata();
        /// <summary>メタデータを検証し、保存用の内容をファイルへ触れずに構築します。</summary>
        static std::string SerializeMetadata(const AssetMetadata& metadata);
        /// <summary>検証済みのメタデータを一時ファイルから置き換えます。</summary>
        static void Write(const std::filesystem::path& path, const AssetMetadata& metadata)
        {
            Write(path, metadata, FileContents::Read(Sidecar(path)));
        }
        /// <summary>読み込み時の内容と照合し、別エディターのインポート設定を上書きしません。</summary>
        static void Write(const std::filesystem::path& path, const AssetMetadata& metadata,
                          const std::optional<std::string>& expected);
        /// <summary>既存のIDを保持し、存在する未登録アセットだけに新しいメタデータを保存します。</summary>
        static AssetMetadata Ensure(const std::filesystem::path& path);
        /// <summary>参照解決にも保存の排他と同じプロジェクト境界を使います。通常の領域外ファイルは空を返します。</summary>
        static std::filesystem::path Root(const std::filesystem::path& path);
        /// <summary>snapshot はプレイ中の参照解決用にメタデータを保持します。編集時は既定の最新読み込みを使います。</summary>
        explicit AssetDatabase(std::filesystem::path root, bool snapshot = false)
            : AssetDatabase(ContentContext::Capture(root), snapshot)
        {
        }
        /// <summary>所有者が固定した素材層で索引を作成し、descriptorを再解決しません。</summary>
        explicit AssetDatabase(std::shared_ptr<const ContentContext> context, bool snapshot = false);
        /// <summary>参照先を解決します。</summary>
        std::filesystem::path Resolve(const std::filesystem::path& path, const std::string& id = {}) const;
        /// <summary>参照先のIDを取得します。スナップショットでは保持済みメタデータだけを使用します。</summary>
        std::string ReferenceId(const std::filesystem::path& path) const;
        /// <summary>指定した対象を移動します。</summary>
        void Move(const std::filesystem::path& from, const std::filesystem::path& to);

    private:
        /// <summary>移動前後の素材の分類を照合します。</summary>
        static bool SameCategory(const std::filesystem::path& from, const std::filesystem::path& to);
        /// <summary>移動先のパスと競合を検証します。</summary>
        void ValidateDestination(const std::filesystem::path& from, const std::filesystem::path& to) const;

        /// <summary>シェーダー移動時に直接includeの相対位置を保持し、書き換えできない指定は移動前に拒否します。</summary>
        static void RelocateIncludeOperand(std::string& line, size_t directive, const std::filesystem::path& from,
                                           const std::filesystem::path& to);

        /// <summary>シェーダーの行内の参照先を更新します。</summary>
        static void RelocateShaderLine(std::string& line, const std::filesystem::path& from,
                                       const std::filesystem::path& to);

        /// <summary>シェーダーの移動に合わせてinclude参照を更新します。</summary>
        static std::string RelocateShaderIncludes(const std::string& original, const std::filesystem::path& from,
                                                  const std::filesystem::path& to);
        /// <summary>glTFの移動に合わせて関連資源の参照を更新します。</summary>
        static std::string RelocateGltfResources(const std::string& original, const std::filesystem::path& from,
                                                 const std::filesystem::path& to);

        /// <summary>移動後のパスに合わせて依存参照を書き換えます。</summary>
        void RewriteDependencies(const std::filesystem::path& from, const std::filesystem::path& to,
                                 std::string& original, std::string& revised) const;
    };
}
