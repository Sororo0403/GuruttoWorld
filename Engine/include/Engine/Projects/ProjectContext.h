#pragma once
#include <Engine/Core/Json.h>
#include <Engine/Core/BuildIdentity.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

namespace Engine
{
    struct ProjectContext
    {
        static constexpr unsigned int ContractVersion = 1;
        std::filesystem::path root, engineRoot;
        std::string name, projectId;
        bool generatedProjects = false;

        /// <summary>descriptor と共通で使うゲーム名の規則を確認します。</summary>
        static bool ValidName(const std::string& value);

        /// <summary>設定保存先に使用できる安定したプロジェクト ID か確認します。</summary>
        static bool ValidIdentity(const std::string& value);

        /// <summary>UTF-8 の descriptor パスをファイルシステムのパスへ変換します。</summary>
        static std::filesystem::path Path(const std::string& value);

        /// <summary>descriptor の相対参照を、実際のチェックアウトに対して一度だけ解決します。</summary>
        static std::filesystem::path ResolveEngine(const std::filesystem::path& root, const Json& descriptor)
        {
            const auto engine = Path(descriptor.at("engineRoot").get<std::string>());
            return std::filesystem::weakly_canonical(engine.is_absolute() ? engine : root / engine);
        }

        /// <summary>開発時の標準シェーダー参照先を求めます。配布物は同梱リソースを使います。</summary>
        static std::filesystem::path ShaderRoot(const std::filesystem::path& root);

        /// <summary>旧プロジェクトでは Project.json のアセット ID を引き継ぎ、設定の保存先を維持します。</summary>
        static std::string Identity(const std::filesystem::path& root);

        /// <summary>ゲームの descriptor を検証し、派生ファイルと実行処理で共有する参照を取得します。</summary>
        static ProjectContext Load(std::filesystem::path path);

        /// <summary>ホストのビルドに記録したソース参照先を取得します。</summary>
        static std::filesystem::path SourceEngineRoot()
        {
            return BuildSourceRoot();
        }

        /// <summary>異なるチェックアウトのホストと標準シェーダー・DLLを混在させる前に拒否します。</summary>
        void RequireEngine(const std::filesystem::path& host) const;
    };
}
