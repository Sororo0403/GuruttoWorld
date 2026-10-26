#include <Engine/Assets/AssetArtifact.h>
#include "ToolCommon.h"
#include "ProjectFiles.h"
#include "ScriptBuild.h"
#include "PackageBuild.h"
#include <algorithm>
#include <cwctype>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

namespace
{
    using namespace ProjectTool;

    Path DefaultEngine()
    {
        auto current = CurrentExecutable().parent_path();
        while (!current.empty())
        {
            if (std::filesystem::is_regular_file(current / "WP1.slnx"))
                return current;
            const auto parent = current.parent_path();
            if (parent == current)
                break;
            current = parent;
        }
        throw std::runtime_error("Pass --engine <engine checkout>.");
    }

    void Help()
    {
        std::cout << "ProjectTool <command> [options]\n"
                     "  new-project --destination <folder> [--name MyGame] [--template-content <folder>]\n"
                     "  sync-project --project <folder>\n"
                     "  project-context --project <folder>\n"
                     "  sync-content --content <folder> --destination <folder>\n"
                     "  snapshot --content <folder> --destination <folder> [--no-engine-resources]\n"
                     "  build-scripts --content <folder> [--configuration Development] [--output-content <folder>]\n"
                     "  build-player --project <folder> [--configuration Release] [--skip-build]\n"
                     "Common options: --engine <checkout>, --work-root <folder>, --run-id <id>\n";
    }
}

int wmain(int argc, wchar_t** argv)
{
    Engine::AssetImportScope imports;
    try
    {
        using namespace ProjectTool;
        if (argc < 2 || std::wstring_view(argv[1]) == L"--help" || std::wstring_view(argv[1]) == L"-h")
        {
            Help();
            return argc < 2 ? 1 : 0;
        }
        const std::wstring command = argv[1];
        const std::set<std::wstring> commands{L"new-project", L"sync-project",  L"project-context", L"sync-content",
                                              L"snapshot",    L"build-scripts", L"build-player"};
        if (!commands.contains(command))
            throw std::runtime_error("Unknown command.");
        std::map<std::wstring, std::wstring> arguments;
        const std::set<std::wstring> switches{L"skipbuild", L"noengineresources"};
        const std::set<std::wstring> names{L"engine",        L"engineroot",       L"content",       L"destination",
                                           L"outputcontent", L"project",          L"configuration", L"workroot",
                                           L"runid",         L"startupscene",     L"name",          L"templatecontent",
                                           L"skipbuild",     L"noengineresources"};
        for (int index = 2; index < argc; ++index)
        {
            std::wstring key = argv[index];
            if (key.empty() || key.front() != L'-')
                throw std::runtime_error("Expected an option beginning with --.");
            key.erase(std::remove(key.begin(), key.end(), L'-'), key.end());
            std::transform(key.begin(), key.end(), key.begin(),
                           [](wchar_t c)
                           {
                               return static_cast<wchar_t>(std::towlower(c));
                           });
            if (!names.contains(key) || arguments.contains(key))
                throw std::runtime_error("Unknown or duplicate option: " + Utf8(Path(argv[index])));
            if (switches.contains(key))
                arguments[key] = L"true";
            else
            {
                if (++index >= argc)
                    throw std::runtime_error("Missing option value.");
                arguments[key] = argv[index];
            }
        }
        const auto value = [&](const wchar_t* key, const std::wstring& fallback = {})
        {
            const auto found = arguments.find(key);
            return found == arguments.end() ? fallback : found->second;
        };
        const auto directory = [&](const wchar_t* key, const Path& fallback = {})
        {
            const auto text = value(key);
            return text.empty() ? fallback : std::filesystem::absolute(Path(text)).lexically_normal();
        };
        Options options;
        options.engine = directory(L"engine", directory(L"engineroot"));
        if (options.engine.empty())
            options.engine = DefaultEngine();
        options.project = directory(L"project");
        options.content =
            directory(L"content", options.project.empty() ? std::filesystem::current_path() : options.project);
        if (options.content.filename() == "WP1.project.json")
            options.content = options.content.parent_path();
        options.output = directory(L"outputcontent", options.content);
        options.work = directory(L"workroot", options.engine);
        options.configuration =
            Utf8(Path(value(L"configuration", command == L"build-player" ? L"Release" : L"Development")));
        options.runId = Utf8(Path(value(L"runid", Wide(NewId()))));
        options.startupScene = Utf8(Path(value(L"startupscene")));
        options.skipBuild = arguments.contains(L"skipbuild");
        if (command == L"build-scripts")
        {
            if (!arguments.contains(L"content") && !arguments.contains(L"project") &&
                !std::filesystem::is_regular_file(options.content / "WP1.project.json"))
                throw std::runtime_error("Pass --content <game project folder>.");
            BuildScripts(options);
        }
        else if (command == L"build-player")
            BuildPackage(options);
        else if (command == L"new-project")
            NewProject(directory(L"destination"), Utf8(Path(value(L"name", L"MyGame"))), directory(L"templatecontent"),
                       options.engine);
        else if (command == L"sync-project")
            SyncProject(options.project.empty() ? options.content : options.project, options.engine);
        else if (command == L"project-context")
            std::cout
                << LoadProject(options.project.empty() ? options.content : options.project, options.engine).dump(2)
                << '\n';
        else if (command == L"sync-content")
            SyncContent(options.content, directory(L"destination"), options.engine);
        else if (command == L"snapshot")
            SnapshotContent(options.content, directory(L"destination"), !arguments.contains(L"noengineresources"));
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
