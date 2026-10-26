#include "ProjectFiles.h"
#include <Windows.h>
#include <algorithm>
#include <cwctype>
#include <fstream>
#include <map>
#include <regex>
#include <stdexcept>
#include <vector>

namespace ProjectTool
{
    namespace
    {
        Path FullPath(const Path& path)
        {
            if (path.empty())
                throw std::runtime_error("A directory path is required.");
            auto result = std::filesystem::absolute(path).lexically_normal().make_preferred();
            while (result != result.root_path() && result.filename().empty())
                result = result.parent_path();
            return result;
        }

        Path ProjectRoot(const Path& project)
        {
            const auto root = FullPath(project);
            return root.filename() == L"WP1.project.json" ? root.parent_path() : root;
        }

        std::string ForwardPath(const Path& path)
        {
            auto text = Utf8(path);
            std::replace(text.begin(), text.end(), '\\', '/');
            return text;
        }

        bool SamePath(const Path& left, const Path& right)
        {
            const auto leftText = FullPath(left).native();
            const auto rightText = FullPath(right).native();
            return CompareStringOrdinal(leftText.data(), static_cast<int>(leftText.size()), rightText.data(),
                                        static_cast<int>(rightText.size()), TRUE) == CSTR_EQUAL;
        }

        bool Contains(const Path& parent, const Path& child)
        {
            auto prefix = FullPath(parent).native();
            const auto value = FullPath(child).native();
            if (SamePath(parent, child))
                return true;
            if (prefix.back() != L'\\')
                prefix += L'\\';
            return value.size() >= prefix.size() &&
                   CompareStringOrdinal(prefix.data(), static_cast<int>(prefix.size()), value.data(),
                                        static_cast<int>(prefix.size()), TRUE) == CSTR_EQUAL;
        }

        struct PathLess
        {
            bool operator()(const std::string& left, const std::string& right) const
            {
                const auto leftWide = Wide(left);
                const auto rightWide = Wide(right);
                return CompareStringOrdinal(leftWide.data(), static_cast<int>(leftWide.size()), rightWide.data(),
                                            static_cast<int>(rightWide.size()), TRUE) == CSTR_LESS_THAN;
            }
        };

        void RejectLinks(const Path& path)
        {
            for (auto current = FullPath(path);; current = current.parent_path())
            {
                const auto attributes = GetFileAttributesW(current.c_str());
                if (attributes == INVALID_FILE_ATTRIBUTES)
                {
                    const auto error = GetLastError();
                    if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
                        throw std::runtime_error("Cannot inspect content path: " + Utf8(current));
                }
                else if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                    throw std::runtime_error("Content operations do not follow links: " + Utf8(current));
                if (current == current.root_path())
                    break;
            }
        }

        std::vector<Path> Files(const Path& root)
        {
            RejectLinks(root);
            std::vector<Path> files;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
            {
                const auto attributes = GetFileAttributesW(entry.path().c_str());
                if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                    throw std::runtime_error("Content operations do not follow links: " + Utf8(entry.path()));
                if (entry.is_regular_file())
                    files.push_back(entry.path());
            }
            std::sort(files.begin(), files.end());
            return files;
        }

        bool Generated(const std::string& relative)
        {
            auto value = Wide(relative);
            std::transform(value.begin(), value.end(), value.begin(),
                           [](wchar_t ch)
                           {
                               return std::towlower(ch);
                           });
            return value.starts_with(L"assets/scripts/bin/") || value.starts_with(L"shaders/compiled/") ||
                   value.starts_with(L"engine/shaders/compiled/");
        }

        bool Temporary(const Path& file)
        {
            return std::regex_search(Utf8(file.filename()), std::regex(R"(\.tmp($|\.))", std::regex::icase));
        }

        bool Identifier(const std::string& name)
        {
            return std::regex_match(name, std::regex("[A-Za-z][A-Za-z0-9_]{0,63}"));
        }

        bool Identity(const Json& value)
        {
            return value.is_string() && std::regex_match(value.get<std::string>(), std::regex("[a-f0-9]{32}"));
        }

        void RequireVersion(const Json& document, const char* key, const char* message)
        {
            if (!document.contains(key) || !document.at(key).is_number_integer() || document.at(key) != 1)
                throw std::runtime_error(message);
        }

        Path EngineRoot(const Path& content)
        {
            const auto descriptor = ReadJson(content / "WP1.project.json");
            if (!descriptor.contains("engineRoot") || !descriptor.at("engineRoot").is_string())
                throw std::runtime_error("Game project has no valid engine reference.");
            const auto text = descriptor.at("engineRoot").get<std::string>();
            if (text.find('\0') != std::string::npos || text.find_first_not_of(" \t\r\n") == std::string::npos)
                throw std::runtime_error("Game project has no valid engine reference.");
            const auto path = FromUtf8(text);
            return FullPath(path.is_absolute() ? path : content / path);
        }

        void WriteIfChanged(const Path& path, const std::string& source)
        {
            std::ifstream input(path, std::ios::binary);
            const std::string previous{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
            if (input && previous == source)
                return;
            input.close();
            const auto temporary = Path(path.native() + L".tmp." + Wide(NewId()));
            try
            {
                WriteText(temporary, source);
                if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                    throw std::runtime_error("Cannot publish project file: " + Utf8(path));
            }
            catch (...)
            {
                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);
                throw;
            }
        }

        void WriteRuntimeContext(const Path& content, const Path& destination)
        {
            std::string identity;
            if (std::filesystem::is_regular_file(content / "WP1.project.json"))
            {
                const auto descriptor = ReadJson(content / "WP1.project.json");
                if (descriptor.contains("projectId"))
                    identity = LoadProject(content).at("projectId").get<std::string>();
            }
            else if (std::filesystem::is_regular_file(content / "WP1.runtime.json"))
            {
                const auto runtime = ReadJson(content / "WP1.runtime.json");
                RequireVersion(runtime, "version", "Unsupported runtime project context.");
                if (!runtime.contains("projectId") || !Identity(runtime.at("projectId")))
                    throw std::runtime_error("Invalid runtime project ID.");
                identity = runtime.at("projectId").get<std::string>();
            }
            const auto target = destination / "WP1.runtime.json";
            if (identity.empty())
                std::filesystem::remove(target);
            else
                WriteIfChanged(target, Json{{"version", 1}, {"projectId", identity}}.dump(2) + "\r\n");
        }

        struct Entry
        {
            Path source;
            std::uintmax_t bytes;
            std::string hash;
            bool inputOnly = false;

            bool operator==(const Entry&) const = default;
        };

        using Inventory = std::map<std::string, Entry, PathLess>;

        void CheckBoundary(const Path& source, const Path& destination)
        {
            if (destination == destination.root_path())
                throw std::runtime_error("A volume root cannot be used as a content output directory.");
            RejectLinks(destination);
            for (const auto* folder : {"Assets", "Shaders", "Engine/Shaders"})
            {
                if (Contains(source, destination) || Contains(destination / folder, source))
                    throw std::runtime_error("Source and destination content folders must not overlap.");
            }
        }

        Inventory ContentInventory(const Path& content, const Path& destination, const Path& engine,
                                   bool includeEngineResources, bool contexts)
        {
            if (!std::filesystem::is_directory(content / "Assets"))
                throw std::runtime_error("Content Assets folder is missing.");
            Inventory entries;
            for (const auto* folder : {"Assets", "Shaders", "Engine/Shaders"})
            {
                const bool engineShaders = std::string_view(folder) == "Engine/Shaders";
                if (engineShaders && !includeEngineResources)
                    continue;
                const auto source = (engineShaders && !engine.empty() ? engine : content) / folder;
                CheckBoundary(source, destination);
                if (!std::filesystem::is_directory(source))
                {
                    if (engineShaders && !engine.empty())
                        throw std::runtime_error("Engine shaders are missing: " + Utf8(source));
                    continue;
                }
                for (const auto& file : Files(source))
                {
                    const auto relative = std::string(folder) + "/" + ForwardPath(file.lexically_relative(source));
                    if (!Generated(relative) && !Temporary(file))
                        entries[relative] = {file, std::filesystem::file_size(file), Sha256(file)};
                }
            }
            if (contexts)
            {
                for (const auto* file : {"WP1.project.json", "WP1.runtime.json"})
                {
                    const auto source = content / file;
                    if (std::filesystem::is_regular_file(source))
                    {
                        RejectLinks(source);
                        entries[std::string("@context/") + file] = {source, std::filesystem::file_size(source),
                                                                    Sha256(source), true};
                    }
                }
            }
            return entries;
        }

        void CopyEntry(const Entry& entry, const Path& target)
        {
            std::filesystem::create_directories(target.parent_path());
            std::filesystem::copy_file(entry.source, target, std::filesystem::copy_options::overwrite_existing);
            if (Sha256(target) != entry.hash)
                throw std::runtime_error("Content changed while copying: " + Utf8(entry.source));
        }
    }

    Json LoadProject(const Path& project, const Path& engine)
    {
        const auto root = ProjectRoot(project);
        auto descriptor = ReadJson(root / "WP1.project.json");
        RequireVersion(descriptor, "version", "Unsupported game project version.");
        if (descriptor.contains("engineContract"))
            RequireVersion(descriptor, "engineContract", "Unsupported engine contract version.");
        if (!descriptor.contains("name") || !descriptor.at("name").is_string())
            throw std::runtime_error("Invalid game project descriptor.");
        const auto name = descriptor.at("name").get<std::string>();
        if (!Identifier(name) || descriptor.value("project", std::string{}) != name + ".vcxproj" ||
            descriptor.value("solution", std::string{}) != name + ".slnx")
            throw std::runtime_error("Invalid game project descriptor.");
        if (descriptor.contains("projectId") && !Identity(descriptor.at("projectId")))
            throw std::runtime_error("Invalid project ID.");
        if (descriptor.contains("generatedProjects") && !descriptor.at("generatedProjects").is_boolean())
            throw std::runtime_error("Invalid generated project flag.");
        const auto resolved = EngineRoot(root);
        if (!engine.empty() && !SamePath(engine, resolved))
            throw std::runtime_error("Game project uses a different engine checkout.");
        descriptor["engineRoot"] = Utf8(resolved);
        return descriptor;
    }

    void SyncProject(const Path& project, const Path& engine)
    {
        const auto root = ProjectRoot(project);
        const auto descriptor = LoadProject(root, engine);
        if (!descriptor.value("generatedProjects", false))
            throw std::runtime_error("This project owns its build files; automatic regeneration is disabled.");
        if (!descriptor.contains("projectId") || !Identity(descriptor.at("projectId")))
            throw std::runtime_error("Generated projects require a stable project ID.");
        const auto name = descriptor.at("name").get<std::string>();
        auto identity = descriptor.at("projectId").get<std::string>();
        std::transform(identity.begin(), identity.end(), identity.begin(),
                       [](char value)
                       {
                           return value >= 'a' && value <= 'f' ? static_cast<char>(value - 'a' + 'A') : value;
                       });
        const auto guid = identity.substr(0, 8) + "-" + identity.substr(8, 4) + "-" + identity.substr(12, 4) + "-" +
                          identity.substr(16, 4) + "-" + identity.substr(20);
        const auto enginePath = FromUtf8(descriptor.at("engineRoot").get<std::string>());
        auto nativeRoot = Utf8(enginePath);
        std::replace(nativeRoot.begin(), nativeRoot.end(), '/', '\\');
        const auto escapedRoot = XmlEscape(nativeRoot + "\\");
        const auto titleFeature = descriptor.value("titleFeature", false) ? "true" : "false";
        const auto projectText =
            "<Project DefaultTargets=\"Build\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\r\n"
            "  <!-- WP1.project.json から生成します。 -->\r\n"
            "  <PropertyGroup>\r\n"
            "    <GameProjectRoot>$(MSBuildThisFileDirectory)</GameProjectRoot>\r\n"
            "    <GameContentRoot>$(MSBuildThisFileDirectory)</GameContentRoot>\r\n"
            "    <GameName>" +
            name +
            "</GameName>\r\n"
            "    <ProjectGuid>{" +
            guid +
            "}</ProjectGuid>\r\n"
            "    <WP1Root>" +
            escapedRoot +
            "</WP1Root>\r\n"
            "    <EnableTitleFeature>" +
            std::string(titleFeature) +
            "</EnableTitleFeature>\r\n"
            "  </PropertyGroup>\r\n"
            "  <Import Project=\"$(WP1Root)Engine\\build\\GameProject.targets\" />\r\n"
            "</Project>\r\n";
        std::string solutionText = "<Solution>\r\n"
                                   "  <!-- WP1.project.json から生成します。 -->\r\n"
                                   "  <Configurations>\r\n"
                                   "    <BuildType Name=\"Debug\" />\r\n"
                                   "    <BuildType Name=\"Development\" />\r\n"
                                   "    <BuildType Name=\"Release\" />\r\n"
                                   "    <Platform Name=\"x64\" />\r\n"
                                   "  </Configurations>\r\n"
                                   "  <Project Path=\"" +
                                   name + ".vcxproj\" />\r\n";
        for (const auto* reference : {"Player/Player.vcxproj", "Engine/Engine.vcxproj", "Engine/EngineCore.vcxproj",
                                      "Engine/EngineAssets.vcxproj", "Engine/EnginePlatform.vcxproj",
                                      "SceneRuntime/SceneRuntime.vcxproj", "SceneRuntime/SceneData.vcxproj",
                                      "SceneRuntime/SceneRendering.vcxproj", "SceneRuntime/SceneSimulation.vcxproj",
                                      "SceneRuntime/SceneStorage.vcxproj", "SceneRuntime/SceneRuntimeCore.vcxproj"})
            solutionText += "  <Project Path=\"" + XmlEscape(ForwardPath(enginePath / reference)) + "\" />\r\n";
        if (descriptor.value("titleFeature", false))
            solutionText += "  <Project Path=\"" +
                            XmlEscape(ForwardPath(enginePath / "SceneRuntime/SceneTitle.vcxproj")) + "\" />\r\n";
        solutionText += "</Solution>\r\n";
        RejectLinks(root / (name + ".vcxproj"));
        RejectLinks(root / (name + ".slnx"));
        WriteIfChanged(root / (name + ".vcxproj"), projectText);
        WriteIfChanged(root / (name + ".slnx"), solutionText);
    }

    void NewProject(const Path& destination, const std::string& name, const Path& templateRoot, const Path& engine)
    {
        if (!Identifier(name))
            throw std::runtime_error("Game name must be an identifier of at most 64 characters.");
        const auto target = FullPath(destination);
        const auto source =
            FullPath(templateRoot.empty() ? engine / "Tools/GameProject/Templates/MinimalGame" : templateRoot);
        if (std::filesystem::exists(target))
            throw std::runtime_error("Choose a new folder; existing projects are never overwritten.");
        RejectLinks(target);
        if (!std::filesystem::is_regular_file(source / "Assets/Project.json"))
            throw std::runtime_error("Template is missing Assets/Project.json");
        auto entrySource = source / "src";
        if (!std::filesystem::is_regular_file(entrySource / "main.cpp"))
            entrySource = engine / "Tools/GameProject/Templates/MinimalGame/src";
        const auto entryFiles = Files(entrySource);
        SnapshotContent(source, target, false);
        for (const auto& file : entryFiles)
        {
            const auto entryTarget = target / "src" / file.lexically_relative(entrySource);
            std::filesystem::create_directories(entryTarget.parent_path());
            std::filesystem::copy_file(file, entryTarget);
        }
        auto settings = ReadJson(target / "Assets/Project.json");
        settings["importLegacySettings"] = false;
        WriteJsonAtomic(target / "Assets/Project.json", settings);
        const auto metadataPath = target / "Assets/Project.json.meta";
        auto metadata =
            std::filesystem::exists(metadataPath)
                ? ReadJson(metadataPath)
                : Json{{"version", 1}, {"id", ""}, {"scale", 1}, {"flipV", false}, {"previousPaths", Json::array()}};
        const auto identity = NewId();
        metadata["id"] = identity;
        WriteJsonAtomic(metadataPath, metadata);
        const Json descriptor{{"version", 1},
                              {"engineContract", 1},
                              {"name", name},
                              {"projectId", identity},
                              {"engineRoot", Utf8(FullPath(engine))},
                              {"project", name + ".vcxproj"},
                              {"solution", name + ".slnx"},
                              {"generatedProjects", true}};
        WriteJsonAtomic(target / "WP1.project.json", descriptor);
        SyncProject(target, engine);
        WriteText(target / ".gitignore", "generated/\r\nAssets/Scripts/Bin/\r\n*.vcxproj.user\r\n.vs/\r\n");
    }

    void SyncContent(const Path& content, const Path& destination, const Path& engine)
    {
        const auto source = FullPath(content);
        const auto target = FullPath(destination);
        auto resources = engine.empty() ? Path{} : FullPath(engine);
        if (resources.empty() && std::filesystem::is_regular_file(source / "WP1.project.json"))
            resources = EngineRoot(source);
        const auto entries = ContentInventory(source, target, resources, true, false);
        RejectLinks(target / "WP1.runtime.json");
        std::vector<Path> outputFiles;
        for (const auto* folder : {"Assets", "Shaders", "Engine/Shaders"})
        {
            const auto output = target / folder;
            RejectLinks(output);
            if (std::filesystem::exists(output))
            {
                const auto files = Files(output);
                outputFiles.insert(outputFiles.end(), files.begin(), files.end());
            }
        }
        // 正本と出力の境界、すべてのリンクを確認してから書き換えを始めます。
        for (const auto& [relative, entry] : entries)
        {
            const auto output = target / FromUtf8(relative);
            if (!std::filesystem::is_regular_file(output) || Sha256(output) != entry.hash)
                CopyEntry(entry, output);
        }
        // DLLとシェーダーキャッシュの寿命は、それぞれの生成処理が管理します。
        for (const auto& file : outputFiles)
        {
            const auto relative = ForwardPath(file.lexically_relative(target));
            if (!Generated(relative) && !entries.contains(relative))
                std::filesystem::remove(file);
        }
        std::filesystem::create_directories(target);
        WriteRuntimeContext(source, target);
    }

    void SnapshotContent(const Path& content, const Path& destination, bool includeEngineResources)
    {
        const auto source = FullPath(content);
        const auto target = FullPath(destination);
        Path engine;
        if (includeEngineResources && std::filesystem::is_regular_file(source / "WP1.project.json"))
            engine = EngineRoot(source);
        const auto before = ContentInventory(source, target, engine, includeEngineResources, true);
        if (includeEngineResources)
            RejectLinks(target / "WP1.runtime.json");
        // 既存出力を使う場合にも、コピー対象のリンクを通じて正本を書き換えないようにします。
        for (const auto& [relative, entry] : before)
            if (!entry.inputOnly)
                RejectLinks(target / FromUtf8(relative));
        std::filesystem::create_directories(target);
        for (const auto& [relative, entry] : before)
            if (!entry.inputOnly)
                CopyEntry(entry, target / FromUtf8(relative));
        if (includeEngineResources)
        {
            RejectLinks(target / "WP1.runtime.json");
            WriteRuntimeContext(source, target);
        }
        const auto after = ContentInventory(source, target, engine, includeEngineResources, true);
        if (before != after)
            throw std::runtime_error("Content changed while taking build snapshot. Save and build again.");
    }
}
