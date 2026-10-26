#include "PackageBuild.h"
#include "ProjectFiles.h"
#include "ScriptBuild.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <regex>
#include <stdexcept>

namespace ProjectTool
{
    namespace
    {
        void AppendLog(const Path& path, const std::string& message)
        {
            std::ofstream output(path, std::ios::binary | std::ios::app);
            output << message << "\r\n";
        }

        std::vector<std::wstring> BuildArguments(const Path& project, const std::string& configuration)
        {
            return {project.wstring(),
                    L"/p:Configuration=" + Wide(configuration),
                    L"/p:Platform=x64",
                    L"/p:PreferredToolArchitecture=x64",
                    L"/p:SkipProjectToolBuild=true",
                    L"/p:ProjectToolExecutable=" + CurrentExecutable().wstring(),
                    L"/m:1",
                    L"/nr:false",
                    L"/v:minimal",
                    L"/nologo"};
        }

        std::array<unsigned, 3> RedistVersion(const std::string& version)
        {
            std::array<unsigned, 3> result{};
            size_t offset = 0;
            for (auto& part : result)
            {
                size_t consumed = 0;
                part = static_cast<unsigned>(std::stoul(version.substr(offset), &consumed));
                offset += consumed + 1;
            }
            return result;
        }

        void CopyRuntime(const Path& installation, const Path& destination)
        {
            const auto root = installation / "VC/Redist/MSVC";
            Path latest;
            std::array<unsigned, 3> latestVersion{};
            const std::regex versionPattern(R"(^\d+\.\d+\.\d+$)");
            for (const auto& entry : std::filesystem::directory_iterator(root))
            {
                const auto name = Utf8(entry.path().filename());
                if (!entry.is_directory() || !std::regex_match(name, versionPattern))
                    continue;
                const auto version = RedistVersion(name);
                if (latest.empty() || version > latestVersion)
                {
                    latest = entry.path();
                    latestVersion = version;
                }
            }
            if (latest.empty())
                throw std::runtime_error("MSVC redistributable runtime is missing.");
            const std::regex crtPattern(R"(^Microsoft\.VC\d+\.CRT$)");
            for (const auto& entry : std::filesystem::directory_iterator(latest / "x64"))
            {
                if (!entry.is_directory() || !std::regex_match(Utf8(entry.path().filename()), crtPattern))
                    continue;
                size_t copied = 0;
                for (const auto& file : std::filesystem::directory_iterator(entry.path()))
                {
                    if (file.is_regular_file() && file.path().extension() == ".dll")
                    {
                        std::filesystem::copy_file(file.path(), destination / file.path().filename(),
                                                   std::filesystem::copy_options::overwrite_existing);
                        ++copied;
                    }
                }
                if (copied == 0)
                    throw std::runtime_error("x64 MSVC CRT contains no runtime DLLs.");
                return;
            }
            throw std::runtime_error("x64 MSVC CRT is missing.");
        }

        Path StartupPath(const Path& staging, const std::string& relative)
        {
            const auto path = FromUtf8(relative);
            if (path.empty() || path.has_root_path())
                throw std::runtime_error("Packaged startup scene must be a relative path.");
            for (const auto& component : path)
                if (component == "..")
                    throw std::runtime_error("Packaged startup scene escapes the package.");
            return staging / path;
        }

        std::string ResolveStartup(const Path& staging, const Json& project)
        {
            auto startup = project.at("startupScene").get<std::string>();
            if (std::filesystem::is_regular_file(StartupPath(staging, startup)))
                return startup;
            std::string referenceId;
            if (project.contains("assetReferences") && project.at("assetReferences").contains(startup))
                referenceId = project.at("assetReferences").at(startup).get<std::string>();
            for (const auto& file : std::filesystem::recursive_directory_iterator(staging / "Assets"))
            {
                if (!file.is_regular_file() || file.path().extension() != ".meta")
                    continue;
                const auto metadata = ReadJson(file.path());
                bool matches = !referenceId.empty() && metadata.value("id", std::string{}) == referenceId;
                if (referenceId.empty() && metadata.contains("previousPaths"))
                {
                    const auto& previous = metadata.at("previousPaths");
                    matches = std::find(previous.begin(), previous.end(), startup) != previous.end();
                }
                if (!matches)
                    continue;
                auto assetPath = file.path();
                assetPath.replace_extension();
                startup = Utf8(assetPath.lexically_relative(staging).generic_wstring());
                if (std::filesystem::is_regular_file(StartupPath(staging, startup)))
                    return startup;
            }
            throw std::runtime_error("Packaged startup scene is missing.");
        }

        std::string CreatedUtc()
        {
            SYSTEMTIME now{};
            GetSystemTime(&now);
            std::array<char, 40> text{};
            std::snprintf(text.data(), text.size(), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
                          static_cast<unsigned>(now.wYear), static_cast<unsigned>(now.wMonth),
                          static_cast<unsigned>(now.wDay), static_cast<unsigned>(now.wHour),
                          static_cast<unsigned>(now.wMinute), static_cast<unsigned>(now.wSecond),
                          static_cast<unsigned>(now.wMilliseconds));
            return text.data();
        }

        void WriteManifest(const Path& staging, const Options& options)
        {
            const auto startup = ResolveStartup(staging, ReadJson(staging / "Assets/Project.json"));
            std::vector<Path> files;
            for (const auto& file : std::filesystem::recursive_directory_iterator(staging))
                if (file.is_regular_file())
                    files.push_back(file.path());
            std::sort(files.begin(), files.end());
            auto entries = Json::array();
            for (const auto& file : files)
                entries.push_back({{"path", Utf8(file.lexically_relative(staging).generic_wstring())},
                                   {"bytes", std::filesystem::file_size(file)},
                                   {"sha256", Sha256(file)}});
            WriteJsonAtomic(staging / "package.json", {{"configuration", options.configuration},
                                                       {"createdUtc", CreatedUtc()},
                                                       {"startupScene", startup},
                                                       {"files", entries}});
        }
    }

    void BuildPackage(const Options& requested)
    {
        auto options = requested;
        if (options.configuration != "Release" && options.configuration != "Development")
            throw std::runtime_error("Packages require Release or Development configuration.");
        if (!std::regex_match(options.runId, std::regex("^[a-f0-9]{32}$")))
            throw std::runtime_error("Invalid package build run ID.");
        if (options.project.empty())
            options.project = options.content;
        if (options.project.filename() == "WP1.project.json")
            options.project = options.project.parent_path();
        options.project = std::filesystem::absolute(options.project).lexically_normal();
        options.content = options.project;
        options.work = options.project;
        const auto root = options.work / "generated/builds";
        std::filesystem::create_directories(root);
        const auto log = root / (options.runId + ".log");
        const auto result = root / (options.runId + ".json");
        try
        {
            const auto descriptor = LoadProject(options.project, options.engine);
            if (descriptor.value("generatedProjects", false))
                SyncProject(options.project, options.engine);
            const auto project = options.project / FromUtf8(descriptor.at("project").get<std::string>());
            const auto package = root / (options.configuration + "-" + options.runId);
            const auto staging = Path(package.wstring() + L".building");
            if (std::filesystem::exists(package))
                throw std::runtime_error("Package destination already exists.");
            if (!std::filesystem::create_directory(staging))
                throw std::runtime_error("Staging destination already exists.");
            Progress(options, false, 0, "保存済みContentのスナップショットを作成中");
            SnapshotContent(options.content, staging);
            if (!options.startupScene.empty())
            {
                StartupPath(staging, options.startupScene);
                auto settings = ReadJson(staging / "Assets/Project.json");
                settings["startupScene"] = options.startupScene;
                WriteJsonAtomic(staging / "Assets/Project.json", settings);
            }
            const auto msbuild = FindMsbuild();
            const auto installation = msbuild.parent_path().parent_path().parent_path().parent_path();
            if (!options.skipBuild)
            {
                Progress(options, false, 1, "Appをコンパイル中");
                auto arguments = BuildArguments(project, options.configuration);
                arguments.push_back(L"/p:GameContentRoot=" + options.content.wstring() + L"/");
                const auto code = RunProcess(msbuild, arguments, options.engine, log);
                if (code != 0)
                    throw std::runtime_error("MSBuild failed (" + std::to_string(code) + ").");
            }
            const auto executable = options.project / "generated/outputs/x64" / options.configuration / "App" /
                                    FromUtf8(descriptor.at("name").get<std::string>() + ".exe");
            if (!std::filesystem::is_regular_file(executable))
                throw std::runtime_error("Built game executable is missing.");
            Progress(options, false, 2, "配布用C++ゲーム処理をコンパイル中");
            auto scriptOptions = options;
            scriptOptions.content = staging;
            scriptOptions.output = staging;
            scriptOptions.runId += "-scripts";
            BuildScripts(scriptOptions);
            Progress(options, false, 2, "実行ファイル・ランタイムをパッケージ中");
            std::filesystem::copy_file(executable, staging / "App.exe");
            const auto licenses = executable.parent_path() / "Licenses";
            if (std::filesystem::is_directory(licenses))
                std::filesystem::copy(licenses, staging / "Licenses", std::filesystem::copy_options::recursive);
            // 古いビルド出力の不足を引き継がないよう、SkipBuildでもライセンス原文を配置します。
            auto licenseArguments = BuildArguments(project, options.configuration);
            licenseArguments.push_back(L"/t:CopyThirdPartyLicenses");
            licenseArguments.push_back(L"/p:OutDir=" + staging.wstring() + L"/");
            if (RunProcess(msbuild, licenseArguments, options.engine, log) != 0)
                throw std::runtime_error("Third-party license staging failed.");
            CopyRuntime(installation, staging);
            Progress(options, false, 2, "配布用シェーダーを事前コンパイル中");
            const auto shaderCode = RunProcess(staging / "App.exe", {L"--prepare-shaders", L"."}, staging, log, 180000);
            if (shaderCode != 0)
                throw std::runtime_error("Shader cache preparation failed (" + std::to_string(shaderCode) + ").");
            WriteManifest(staging, options);
            Progress(options, false, 3, "パッケージの起動を検証中");
            const auto validation = RunProcess(staging / "App.exe", {L"--validate-package"}, staging, log, 180000);
            if (validation != 0)
                throw std::runtime_error("Package startup validation failed (" + std::to_string(validation) +
                                         "); see Diagnostics/package-validation.log.");
            std::filesystem::rename(staging, package);
            Progress(options, false, 4, "完了");
            WriteJsonAtomic(result, {{"success", true},
                                     {"package", Utf8(package)},
                                     {"log", Utf8(log)},
                                     {"configuration", options.configuration}});
            AppendLog(log, "Package complete: " + Utf8(package));
        }
        catch (const std::exception& error)
        {
            AppendLog(log, error.what());
            WriteJsonAtomic(result, {{"success", false},
                                     {"error", error.what()},
                                     {"log", Utf8(log)},
                                     {"configuration", options.configuration}});
            throw;
        }
    }
}
