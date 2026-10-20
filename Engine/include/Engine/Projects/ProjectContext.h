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
        static bool ValidName(const std::string& value)
        {
            const auto letter = [](unsigned char c)
            {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
            };
            return !value.empty() && value.size() <= 64 && letter(value.front()) &&
                   std::all_of(value.begin(), value.end(),
                               [&](unsigned char c)
                               {
                                   return letter(c) || (c >= '0' && c <= '9') || c == '_';
                               });
        }

        /// <summary>設定保存先に使用できる安定したプロジェクト ID か確認します。</summary>
        static bool ValidIdentity(const std::string& value)
        {
            return value.size() == 32 && std::all_of(value.begin(), value.end(),
                                                     [](unsigned char c)
                                                     {
                                                         return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                                                     });
        }

        /// <summary>UTF-8 の descriptor パスをファイルシステムのパスへ変換します。</summary>
        static std::filesystem::path Path(const std::string& value)
        {
            if (value.empty() || value.find('\0') != std::string::npos)
                throw std::runtime_error("Empty or invalid project path");
            return std::filesystem::path(std::u8string(value.begin(), value.end()));
        }

        /// <summary>descriptor の相対参照を、実際のチェックアウトに対して一度だけ解決します。</summary>
        static std::filesystem::path ResolveEngine(const std::filesystem::path& root, const Json& descriptor)
        {
            const auto engine = Path(descriptor.at("engineRoot").get<std::string>());
            return std::filesystem::weakly_canonical(engine.is_absolute() ? engine : root / engine);
        }

        /// <summary>共有素材の参照だけを解決します。旧来の素材層専用 descriptor も読み込めます。</summary>
        static std::filesystem::path SharedRoot(const std::filesystem::path& root)
        {
            std::ifstream input(root / "WP1.project.json");
            if (!input)
                return {};
            return std::filesystem::weakly_canonical(ResolveEngine(root, Json::parse(input)) / "Content");
        }

        /// <summary>旧プロジェクトでは Project.json のアセット ID を引き継ぎ、設定の保存先を維持します。</summary>
        static std::string Identity(const std::filesystem::path& root)
        {
            std::string identity;
            std::ifstream descriptor(root / "WP1.project.json");
            if (descriptor)
            {
                const auto document = Json::parse(descriptor);
                if (document.contains("projectId"))
                {
                    identity = document.at("projectId").get<std::string>();
                    if (!ValidIdentity(identity))
                        throw std::runtime_error("Invalid project ID");
                }
            }
            if (identity.empty())
            {
                std::ifstream runtime(root / "WP1.runtime.json");
                if (runtime)
                {
                    const auto document = Json::parse(runtime);
                    if (!document.at("version").is_number_integer() || document.at("version").get<int>() != 1)
                        throw std::runtime_error("Unsupported runtime project context");
                    identity = document.at("projectId").get<std::string>();
                    if (!ValidIdentity(identity))
                        throw std::runtime_error("Invalid runtime project ID");
                }
            }
            if (identity.empty())
            {
                std::ifstream metadata(root / "Assets/Project.json.meta");
                if (metadata)
                    identity = Json::parse(metadata).at("id").get<std::string>();
            }
            if (!ValidIdentity(identity))
                throw std::runtime_error("Project needs a stable project ID or legacy Project.json asset ID");
            return identity;
        }

        /// <summary>ゲームの descriptor を検証し、派生ファイルと実行処理で共有する参照を取得します。</summary>
        static ProjectContext Load(std::filesystem::path path)
        {
            path = std::filesystem::absolute(path).lexically_normal();
            if (path.filename() == "WP1.project.json")
                path = path.parent_path();
            std::ifstream input(path / "WP1.project.json");
            if (!input)
                throw std::runtime_error("WP1.project.json is missing");
            const auto descriptor = Json::parse(input);
            if (!descriptor.at("version").is_number_integer() || descriptor.at("version").get<int>() != 1 ||
                (descriptor.contains("engineContract") &&
                 (!descriptor.at("engineContract").is_number_integer() ||
                  descriptor.at("engineContract").get<unsigned int>() != ContractVersion)))
                throw std::runtime_error("Unsupported project or engine contract version");
            ProjectContext result;
            result.root = std::filesystem::weakly_canonical(path);
            result.engineRoot = ResolveEngine(result.root, descriptor);
            result.name = descriptor.at("name").get<std::string>();
            result.projectId = descriptor.value("projectId", std::string{});
            result.generatedProjects = descriptor.value("generatedProjects", false);
            if (!ValidName(result.name) || descriptor.at("project").get<std::string>() != result.name + ".vcxproj" ||
                descriptor.at("solution").get<std::string>() != result.name + ".slnx" ||
                (descriptor.contains("projectId") && !ValidIdentity(result.projectId)))
                throw std::runtime_error("Invalid game project descriptor");
            return result;
        }

        /// <summary>ホストのビルドに記録したソース参照先を取得します。</summary>
        static std::filesystem::path SourceEngineRoot()
        {
            return BuildSourceRoot();
        }

        /// <summary>異なるチェックアウトのホストと共有素材・DLLを混在させる前に拒否します。</summary>
        void RequireEngine(const std::filesystem::path& host) const
        {
            std::error_code error;
            if (!std::filesystem::equivalent(engineRoot, host, error) || error)
                throw std::runtime_error("Project uses a different engine checkout; open its matching Editor");
        }
    };
}
