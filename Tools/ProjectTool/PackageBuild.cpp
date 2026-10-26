#include "PackageBuild.h"
#include <SceneRuntime/Scripts/ScriptModuleApi.h>
#include "ProjectFiles.h"
#include "ScriptBuild.h"
#include <Engine/Assets/AssetArtifact.h>
#include <Engine/Assets/AssetDatabase.h>
#include <Engine/Animation/SkeletonAssets.h>
#include <Engine/Graphics/Models/ModelAssets.h>
#include <Engine/Assets/TextureAssets.h>
#include <set>
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

        void CookModels(const Path& staging)
        {
            std::vector<Path> models, textures;
            const std::set<std::string> imageExtensions{".png", ".jpg",  ".jpeg", ".bmp", ".gif",
                                                        ".tif", ".tiff", ".ico",  ".dds"};
            for (const auto& file : std::filesystem::recursive_directory_iterator(staging / "Assets"))
            {
                auto extension = file.path().extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(),
                               [](unsigned char c)
                               {
                                   return static_cast<char>(std::tolower(c));
                               });
                if (file.is_regular_file() && (extension == ".obj" || extension == ".gltf" || extension == ".glb"))
                    models.push_back(file.path());
                if (file.is_regular_file() && imageExtensions.contains(extension))
                    textures.push_back(file.path());
            }
            std::string error;
            for (const auto& texture : textures)
                if (!Engine::TextureAssets::Cook(texture, staging / "Assets/Compiled/texture-mips", error))
                    throw std::runtime_error(error);
            for (const auto& model : models)
            {
                if (!Engine::ModelAssets::Cook(model, staging / "Assets/Compiled/static-mesh", error))
                    throw std::runtime_error(error);
                auto extension = model.extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(),
                               [](unsigned char c)
                               {
                                   return static_cast<char>(std::tolower(c));
                               });
                if ((extension == ".gltf" || extension == ".glb") &&
                    !Engine::SkeletonAssets::Cook(model, staging / "Assets/Compiled/skeletal-mesh", error))
                    throw std::runtime_error(error);
            }
        }
        void WriteManifest(const Path& staging, const Options& options)
        {
            const auto settings = ReadJson(staging / "Assets/Project.json");
            const auto startup = ResolveStartup(staging, settings);
            const auto runtime = ReadJson(staging / "WP1.runtime.json");
            const auto modulePath = staging / "Assets/Scripts/Bin" / options.configuration / "module.json";
            const auto module = ReadJson(modulePath);
            const auto fingerprintPath =
                options.engine / "generated/build-identity" / options.configuration / "EngineBuildIdentity.generated.h";
            std::ifstream fingerprintInput(fingerprintPath, std::ios::binary);
            const std::string fingerprintSource(std::istreambuf_iterator<char>(fingerprintInput), {});
            std::smatch fingerprint;
            if (!fingerprintInput || !std::regex_search(fingerprintSource, fingerprint,
                                                        std::regex("EngineBuildFingerprint = 0x([a-f0-9]{16})ULL")))
                throw std::runtime_error("Missing package engine build identity");
            std::vector<Path> files;
            for (const auto& file : std::filesystem::recursive_directory_iterator(staging))
                if (file.is_regular_file())
                    files.push_back(file.path());
            std::sort(files.begin(), files.end());
            auto entries = Json::array();
            auto artifacts = Json::array();
            std::set<std::string> assetIds;
            for (const auto& file : files)
            {
                const auto relative = Utf8(file.lexically_relative(staging).generic_wstring());
                const auto digest = Sha256(file);
                std::string identity = "path:" + relative;
                auto dependencies = Json::array();
                auto metadata = file;
                metadata += ".meta";
                Engine::ArtifactDescriptor descriptor;
                descriptor.kind = "package-source";
                descriptor.sourceRevision = digest;
                descriptor.importerVersion = "WP1-source-copy/v1";
                descriptor.platform = "windows-x64";
                descriptor.settings = options.configuration;
                if (std::filesystem::is_regular_file(metadata))
                {
                    const auto values = ReadJson(metadata);
                    if (values.contains("id"))
                    {
                        identity = values.at("id").get<std::string>();
                        if (identity.empty() || !assetIds.insert(identity).second)
                            throw std::runtime_error("Duplicate or empty asset ID in package: " + relative);
                    }
                    const auto revision = Sha256(metadata);
                    descriptor.dependencies.push_back({"metadata", revision});
                    dependencies.push_back({{"id", "metadata"}, {"revision", revision}});
                }
                descriptor.assetId = identity;
                auto key = Sha256Text(descriptor.Canonical());
                Json recipe;
                if (file.extension() == ".artifact")
                {
                    const auto recipePath = file.parent_path() / (Utf8(file.stem()) + ".recipe.json");
                    recipe = ReadJson(recipePath);
                    if (recipe.at("version") != 1 || recipe.at("key") != Utf8(file.stem()))
                        throw std::runtime_error("Invalid cooked artifact recipe: " + relative);
                    descriptor = {recipe.at("kind"),           recipe.at("assetId"),
                                  recipe.at("sourceRevision"), recipe.at("importerVersion"),
                                  recipe.at("settings"),       recipe.at("platform"),
                                  recipe.at("compiler"),       {}};
                    if (recipe.contains("keyInputs"))
                        for (const auto& input : recipe.at("keyInputs"))
                            descriptor.dependencies.push_back(
                                {input.at("id").get<std::string>(), input.at("revision").get<std::string>()});
                    key = Sha256Text(descriptor.Canonical());
                    if (key != Utf8(file.stem()))
                        throw std::runtime_error("Cooked artifact key mismatch: " + relative);
                    std::ifstream artifact(file, std::ios::binary);
                    const std::string bytes(std::istreambuf_iterator<char>(artifact), {});
                    if (artifact.bad())
                        throw std::runtime_error("Cannot read cooked artifact: " + relative);
                    if (bytes.size() < 136 || bytes.substr(0, 8) != "WP1ART02" || bytes.substr(8, 64) != key ||
                        bytes.substr(72, 64) != Sha256Text(bytes.substr(136)) ||
                        recipe.at("outputRevision") != bytes.substr(72, 64))
                        throw std::runtime_error("Cooked artifact integrity mismatch: " + relative);
                    dependencies = Json::array();
                    for (const auto& input : recipe.at("inputs"))
                    {
                        const auto dependency = std::filesystem::weakly_canonical(
                            file.parent_path() / FromUtf8(recipe.value("inputBase", std::string(".."))) /
                            FromUtf8(input.at("path").get<std::string>()));
                        const auto within = dependency.lexically_relative(std::filesystem::weakly_canonical(staging));
                        const auto expected = input.at("sha256").get<std::string>();
                        const bool matches = expected == "missing" ? !std::filesystem::exists(dependency)
                                                                   : std::filesystem::is_regular_file(dependency) &&
                                                                         Sha256(dependency) == expected;
                        if (within.empty() || within.is_absolute() || *within.begin() == ".." || !matches)
                            throw std::runtime_error("Cooked artifact input missing or changed: " + relative);
                        dependencies.push_back(
                            {{"id", Utf8(dependency.lexically_relative(staging))}, {"revision", input.at("sha256")}});
                    }
                    identity = descriptor.assetId;
                }
                artifacts.push_back({{"key", key},
                                     {"assetId", identity},
                                     {"kind", descriptor.kind},
                                     {"sourceRevision", descriptor.sourceRevision},
                                     {"importerVersion", descriptor.importerVersion},
                                     {"platform", descriptor.platform},
                                     {"compiler", descriptor.compiler},
                                     {"settings", descriptor.settings},
                                     {"dependencies", dependencies},
                                     {"outputs", Json::array({relative})}});
                entries.push_back({{"path", Utf8(file.lexically_relative(staging).generic_wstring())},
                                   {"bytes", std::filesystem::file_size(file)},
                                   {"sha256", digest}});
            }
            WriteJsonAtomic(staging / "package.json", {{"projectId", runtime.at("projectId")},
                                                       {"engineBuild", fingerprint[1].str()},
                                                       {"scriptAbi", SceneRuntime::ScriptModuleAbiVersion},
                                                       {"scriptModule", module},
                                                       {"settings", settings},
                                                       {"executableRevision", Sha256(staging / "App.exe")},
                                                       {"configuration", options.configuration},
                                                       {"createdUtc", CreatedUtc()},
                                                       {"startupScene", startup},
                                                       {"files", entries},
                                                       {"artifactVersion", 2},
                                                       {"artifacts", artifacts}});
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
            Progress(options, false, 2, "配布用アセットを取り込み中");
            CookModels(staging);
            WriteManifest(staging, options);
            Progress(options, false, 3, "パッケージの起動を検証中");
            const auto validation = RunProcess(staging / "App.exe", {L"--validate-package"}, staging, log, 180000);
            if (validation != 0)
                throw std::runtime_error("Package startup validation failed (" + std::to_string(validation) +
                                         "); see Diagnostics/package-validation.log.");
            // CPU取り込みの走査が保持した監視を閉じ、Windowsで配布ルートを改名できる状態に戻します。
            Engine::AssetDatabase::ReleaseIndexCache();
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
