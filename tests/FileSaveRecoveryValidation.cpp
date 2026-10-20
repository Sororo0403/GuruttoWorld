#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>

namespace
{
    std::function<void(const std::string&, const std::filesystem::path&)> checkpoint;

    BOOL WINAPI ObserveInformation(HANDLE file, FILE_INFO_BY_HANDLE_CLASS kind, LPVOID data, DWORD size)
    {
        const auto result = ::SetFileInformationByHandle(file, kind, data, size);
        const auto error = GetLastError();
        if (result && checkpoint)
        {
            if (kind == FileRenameInfo)
            {
                const auto* info = static_cast<FILE_RENAME_INFO*>(data);
                checkpoint("rename", std::wstring(info->FileName, info->FileNameLength / sizeof(wchar_t)));
            }
            else if (kind == FileDispositionInfo)
                checkpoint("delete", {});
        }
        SetLastError(error);
        return result;
    }
    BOOL WINAPI ObserveMove(LPCWSTR from, LPCWSTR to, DWORD flags)
    {
        const auto result = ::MoveFileExW(from, to, flags);
        const auto error = GetLastError();
        if (result && checkpoint)
            checkpoint("publish", to);
        SetLastError(error);
        return result;
    }
}

// 実APIの成功後だけで停止し、管理対象の保存実装をそのまま検証します。
#define SetFileInformationByHandle ObserveInformation
#define MoveFileExW ObserveMove
#include "../Editor/src/FileTransaction.h"
#include <Engine/Assets/AssetDatabase.h>
#include <SceneRuntime/ProjectSettings.h>
#undef MoveFileExW
#undef SetFileInformationByHandle

namespace
{
    using Engine::FileContents;
    using Engine::FileJournal;

    void Require(bool condition, const std::string& message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }
    std::filesystem::path Executable()
    {
        std::wstring path(32768, L'\0');
        const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        Require(length && length < path.size(), "executable path");
        path.resize(length);
        return path;
    }
    class Child final
    {
    public:
        Child(const std::wstring& mode, const std::filesystem::path& root, const std::wstring& extra = L"",
              const std::filesystem::path& executable = {})
        {
            auto command = L"\"" + (executable.empty() ? Executable() : executable).native() + L"\" " + mode + L" \"" +
                           root.native() + L"\" " + extra;
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            Require(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                                   &startup, &process_) != FALSE,
                    "start child");
        }
        ~Child()
        {
            if (WaitForSingleObject(process_.hProcess, 0) != WAIT_OBJECT_0)
            {
                TerminateProcess(process_.hProcess, 99);
                WaitForSingleObject(process_.hProcess, 10000);
            }
            CloseHandle(process_.hProcess);
            CloseHandle(process_.hThread);
        }
        void Wait(DWORD expected = 0)
        {
            Require(WaitForSingleObject(process_.hProcess, 30000) == WAIT_OBJECT_0, "child timeout");
            DWORD code = 0;
            const bool exited = GetExitCodeProcess(process_.hProcess, &code) != FALSE;
            Require(exited && code == expected,
                    "child exit " + std::to_string(code) + ", expected " + std::to_string(expected));
        }

    private:
        PROCESS_INFORMATION process_{};
    };
    std::vector<Editor::FileChange> Plans(const std::filesystem::path& root)
    {
        return {Editor::FileTransaction::Prepare(root / "scene.txt", "new-scene"),
                Editor::FileTransaction::Prepare(root / "material.txt", "new-material")};
    }
    void Seed(const std::filesystem::path& root)
    {
        std::filesystem::create_directories(root);
        FileContents::Write(root / "scene.txt", "old-scene");
        FileContents::Write(root / "material.txt", "old-material");
    }
    void CheckGeneration(const std::filesystem::path& root, bool committed)
    {
        Require(FileContents::Read(root / "scene.txt") == (committed ? "new-scene" : "old-scene"), "scene generation");
        Require(FileContents::Read(root / "material.txt") == (committed ? "new-material" : "old-material"),
                "material generation");
        for (const auto& entry : std::filesystem::directory_iterator(root / ".wp1-transactions"))
            Require(entry.path().extension() != ".pending", "recovery is complete");
    }
    bool Archived(const std::filesystem::path& root, const std::string& text)
    {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root / ".wp1-transactions"))
            if (entry.is_regular_file() && FileContents::Read(entry.path()) == text)
                return true;
        return false;
    }
    void CreateForeign(const std::filesystem::path& path)
    {
        FileContents::Write(path, "foreign-edit", false);
    }
    int RunChild(int argc, wchar_t** argv)
    {
        const std::wstring mode = argv[1];
        const auto root = std::filesystem::absolute(argv[2]);
        if (mode == L"--foreign")
        {
            CreateForeign(root / "scene.txt");
            return 0;
        }
        if (mode == L"--recover")
        {
            FileJournal::Recover(root);
            return 0;
        }
        if (mode == L"--ensure")
        {
            Require(argc == 5, "ensure arguments");
            auto gate = OpenEventW(SYNCHRONIZE, FALSE, argv[3]);
            Require(gate != nullptr, "open metadata gate");
            FileContents::Write(root / (std::wstring(argv[4]) + L".ready"), "ready");
            Require(WaitForSingleObject(gate, 10000) == WAIT_OBJECT_0, "metadata gate timeout");
            CloseHandle(gate);
            Engine::Json ids = Engine::Json::array();
            for (int i = 0; i < 100; ++i)
                ids.push_back(
                    Engine::AssetDatabase::Ensure(root / "Assets/Textures" / (std::to_string(i) + ".png")).id);
            FileContents::Write(root / (std::wstring(argv[4]) + L".json"), ids.dump());
            return 0;
        }
        if (mode == L"--recover-crash")
        {
            checkpoint = [](const std::string& operation, const std::filesystem::path&)
            {
                if (operation == "rename")
                    TerminateProcess(GetCurrentProcess(), 77);
            };
            FileJournal::Recover(root);
            throw std::runtime_error("recovery checkpoint not reached");
        }
        auto plans = Plans(root);
        if (mode == L"--api-gap" || mode == L"--api-committed")
            plans = {Editor::FileTransaction::Prepare(root / "Assets/Scenes/scene.json", "candidate-scene"),
                     Editor::FileTransaction::Prepare(root / "Assets/Materials/value.mat", "candidate-material")};
        if (mode == L"--create-delete" || mode == L"--create-delete-commit")
        {
            plans = {Editor::FileTransaction::Prepare(root / "created.bin", std::string("\xff\0new", 5)),
                     Editor::FileTransaction::Prepare(root / "scene.txt", {})};
        }
        if (mode == L"--project-gap")
        {
            auto json = Engine::Json::parse(*FileContents::Read(root / "Assets/Project.json"));
            json["width"] = 1920;
            plans = {Editor::FileTransaction::Prepare(root / "Assets/Project.json", json.dump())};
        }
        if (mode == L"--meta-gap")
        {
            const auto asset = root / "Assets/Textures/0.png";
            auto metadata = Engine::AssetDatabase::Read(asset);
            metadata.scale = 5;
            plans = {Editor::FileTransaction::Prepare(Engine::AssetDatabase::Sidecar(asset),
                                                      Engine::AssetDatabase::SerializeMetadata(metadata))};
        }
        int renames = 0;
        checkpoint = [&](const std::string& operation, const std::filesystem::path& to)
        {
            if (operation == "rename")
            {
                ++renames;
                if (mode == L"--gap" || mode == L"--api-gap" || mode == L"--project-gap" || mode == L"--meta-gap" ||
                    mode == L"--create-delete" || (mode == L"--mixed" && renames == 2) ||
                    (mode == L"--second-gap" && renames == 3) || (mode == L"--all-installed" && renames == 4))
                    TerminateProcess(GetCurrentProcess(), 77);
                if (mode == L"--rollback-gap" && renames >= 5)
                    TerminateProcess(GetCurrentProcess(), 77);
            }
            if ((mode == L"--manifest" && operation == "publish" && to.filename() == "manifest.json") ||
                ((mode == L"--committed" || mode == L"--api-committed" || mode == L"--create-delete-commit") &&
                 operation == "publish" && to.filename() == "committed") ||
                (mode == L"--cleanup" && operation == "delete"))
                TerminateProcess(GetCurrentProcess(), 77);
        };
        std::string error;
        Editor::FileTransaction::Apply(plans, true, error,
                                       [&]
                                       {
                                           return mode != L"--rollback-gap";
                                       });
        throw std::runtime_error("save checkpoint not reached: " + error);
    }
    void PublicSaveApis(const std::filesystem::path& root)
    {
        const auto executable =
            Executable().parent_path().parent_path() / "FileSaveApiValidation/FileSaveApiValidation.exe";
        for (const auto* mode : {L"--api-gap", L"--api-committed"})
        {
            const auto directory = root / mode;
            Seed(directory);
            Child(L"--seed-api", directory, L"", executable).Wait();
            Child(mode, directory).Wait(77);
            Child(L"--save-api", directory, L"", executable).Wait();
            const auto scene = FileContents::Read(directory / "Assets/Scenes/scene.json");
            const auto material = FileContents::Read(directory / "Assets/Materials/value.mat");
            Require(scene && scene->find("saved-object") != std::string::npos && material &&
                        Engine::Json::parse(*material).at("roughness").get<float>() == .23f,
                    "public APIs saved the requested generation");
            FileJournal::Recover(directory);
            FileJournal::Recover(directory);
            Require(FileContents::Read(directory / "Assets/Scenes/scene.json") == scene &&
                        FileContents::Read(directory / "Assets/Materials/value.mat") == material,
                    "public API saves survive later recovery");
        }
        const auto directory = root / "no-overwrite";
        Seed(directory);
        Child(L"--seed-api", directory, L"", executable).Wait();
        const auto before = FileContents::Read(directory / "Assets/Scenes/scene.json");
        Child(L"--api-gap", directory).Wait(77);
        Child(L"--no-overwrite-api", directory, L"", executable).Wait();
        Require(FileContents::Read(directory / "Assets/Scenes/scene.json") == before,
                "no-overwrite recovers before refusing an existing scene");
        std::cout << "PASS: public Scene/Material saves recover first and survive reopening\n";
    }
    void Crashes(const std::filesystem::path& root)
    {
        for (const auto* mode : {L"--manifest", L"--gap", L"--mixed", L"--second-gap", L"--all-installed",
                                 L"--committed", L"--cleanup", L"--rollback-gap"})
        {
            const auto directory = root / mode;
            Seed(directory);
            Child(mode, directory).Wait(77);
            Child(L"--recover", directory).Wait();
            Child(L"--recover", directory).Wait();
            CheckGeneration(directory, std::wstring(mode) == L"--committed" || std::wstring(mode) == L"--cleanup");
        }
        const auto creation = root / "create-delete";
        Seed(creation);
        Child(L"--create-delete", creation).Wait(77);
        Child(L"--recover", creation).Wait();
        Require(!FileContents::Read(creation / "created.bin") &&
                    FileContents::Read(creation / "scene.txt") == "old-scene",
                "creation/deletion rollback after crash");
        const auto committed = root / "create-delete-committed";
        Seed(committed);
        Child(L"--create-delete-commit", committed).Wait(77);
        Child(L"--recover", committed).Wait();
        Require(FileContents::Read(committed / "created.bin") == std::string("\xff\0new", 5) &&
                    !FileContents::Read(committed / "scene.txt"),
                "binary creation/deletion rolls forward");

        const auto project = root / "project-startup";
        Seed(project);
        std::filesystem::create_directories(project / "Assets");
        FileContents::Write(project / "Assets/Project.json", Engine::Json{{"title", "Recovery"},
                                                                          {"startupScene", "Assets/Scenes/Empty.json"},
                                                                          {"width", 1280},
                                                                          {"height", 720}}
                                                                 .dump());
        Child(L"--project-gap", project).Wait(77);
        Require(SceneRuntime::ProjectSettings::Load(project).width == 1280,
                "project load recovers before reading settings");

        const auto interrupted = root / "interrupted-recovery";
        Seed(interrupted);
        Child(L"--mixed", interrupted).Wait(77);
        Child(L"--recover-crash", interrupted).Wait(77);
        Child(L"--recover", interrupted).Wait();
        CheckGeneration(interrupted, false);
        std::cout
            << "PASS: crash at manifest, both rename gaps, partial install, commit, cleanup and interrupted recovery\n";
    }
    void Conflicts(const std::filesystem::path& root)
    {
        for (const bool rollback : {false, true})
        {
            const auto directory = root / (rollback ? "rollback-foreign" : "apply-foreign");
            Seed(directory);
            const auto plans = Plans(directory);
            bool injected = false;
            checkpoint = [&](const std::string& operation, const std::filesystem::path& to)
            {
                if (operation == "rename" && !injected &&
                    to.native().starts_with((directory / "scene.txt").native() + L".wp1-") &&
                    to.native().ends_with(rollback ? L"-stage" : L"-backup"))
                {
                    injected = true;
                    Child(L"--foreign", directory).Wait();
                }
            };
            std::string error;
            const bool accepted = Editor::FileTransaction::Apply(plans, true, error,
                                                                 [rollback]
                                                                 {
                                                                     return !rollback;
                                                                 });
            checkpoint = {};
            Require(injected && !accepted && !error.empty(), "foreign creation rejected and reported");
            CheckGeneration(directory, false);
            Require(Archived(directory, "foreign-edit") && Archived(directory, "new-scene"),
                    "all conflicting versions retained");
            Child(L"--recover", directory).Wait();
            Require(Editor::FileTransaction::Apply(Plans(directory), true, error,
                                                   []
                                                   {
                                                       return true;
                                                   }),
                    "save retry after recovery");
        }
        const auto blocked = root / "blocked-recovery";
        Seed(blocked);
        Child(L"--gap", blocked).Wait(77);
        CreateForeign(blocked / "scene.txt");
        auto file = CreateFileW((blocked / "scene.txt").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                0, nullptr);
        Require(file != INVALID_HANDLE_VALUE, "foreign reader");
        bool rejected = false;
        try
        {
            Editor::FileTransaction::Prepare(blocked / "scene.txt", "overwrite");
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        CloseHandle(file);
        Require(rejected && FileContents::Read(blocked / "scene.txt") == "foreign-edit",
                "pending recovery blocks next save");
        Child(L"--recover", blocked).Wait();
        CheckGeneration(blocked, false);
        Require(Archived(blocked, "foreign-edit") && Archived(blocked, "new-scene"), "crash conflict archive");
        const auto interrupted = root / "interrupted-conflict";
        Seed(interrupted);
        Child(L"--gap", interrupted).Wait(77);
        CreateForeign(interrupted / "scene.txt");
        Child(L"--recover-crash", interrupted).Wait(77);
        Child(L"--recover", interrupted).Wait();
        CheckGeneration(interrupted, false);
        Require(Archived(interrupted, "foreign-edit"), "external content survives interrupted recovery");
        const auto identical = root / "identical-foreign";
        Seed(identical);
        FileContents::Remove(identical / "scene.txt");
        auto creation =
            std::vector<Editor::FileChange>{Editor::FileTransaction::Prepare(identical / "scene.txt", "foreign-edit")};
        checkpoint = [&](const std::string& operation, const std::filesystem::path& to)
        {
            if (operation == "publish" && to.filename() == "manifest.json")
                Child(L"--foreign", identical).Wait();
        };
        std::string error;
        const auto saved = Editor::FileTransaction::Apply(creation, true, error,
                                                          []
                                                          {
                                                              return true;
                                                          });
        checkpoint = {};
        Require(!saved && !FileContents::Read(identical / "scene.txt") && Archived(identical, "foreign-edit"),
                "external file identical to candidate is preserved by file identity");
        bool mapped = false;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(identical / ".wp1-transactions"))
            if (entry.path().filename().string().ends_with(".info.json"))
                mapped = Engine::Json::parse(*FileContents::Read(entry.path())).at("source").get<std::string>() ==
                         FileContents::Text(identical / "scene.txt");
        Require(mapped, "conflict archive records the original path");

        const auto sameIdentity = root / "inplace-foreign";
        Seed(sameIdentity);
        Child(L"--manifest", sameIdentity).Wait(77);
        file = CreateFileW((sameIdentity / "scene.txt").c_str(), GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        Require(file != INVALID_HANDLE_VALUE, "in-place external write");
        DWORD count = 0;
        Require(WriteFile(file, "new-scene", 9, &count, nullptr) && count == 9, "in-place bytes");
        CloseHandle(file);
        Child(L"--recover", sameIdentity).Wait();
        CheckGeneration(sameIdentity, false);
        Require(Archived(sameIdentity, "new-scene"), "in-place change is not mistaken for the staged candidate");
        std::cout
            << "PASS: second-process creation during Apply/Restore, durable conflict archive, blocked recovery and retry\n";
    }
    void Metadata(const std::filesystem::path& root)
    {
        std::filesystem::create_directories(root / "Assets/Textures");
        for (int i = 0; i < 100; ++i)
            FileContents::Write(root / "Assets/Textures" / (std::to_string(i) + ".png"), "fixture");
        const auto name = L"Local\\WP1.MetadataTest." + std::to_wstring(GetCurrentProcessId());
        auto gate = CreateEventW(nullptr, TRUE, FALSE, name.c_str());
        Require(gate != nullptr, "metadata gate");
        Child first(L"--ensure", root, name + L" first"), second(L"--ensure", root, name + L" second");
        const auto deadline = GetTickCount64() + 10000;
        while ((!std::filesystem::exists(root / "first.ready") || !std::filesystem::exists(root / "second.ready")) &&
               GetTickCount64() < deadline)
            Sleep(5);
        Require(std::filesystem::exists(root / "first.ready") && std::filesystem::exists(root / "second.ready"),
                "metadata workers ready");
        SetEvent(gate);
        first.Wait();
        second.Wait();
        CloseHandle(gate);
        const auto one = Engine::Json::parse(*FileContents::Read(root / "first.json"));
        const auto two = Engine::Json::parse(*FileContents::Read(root / "second.json"));
        Require(one == two && one.size() == 100, "both processes return identical asset IDs");
        Engine::AssetDatabase index(root);
        for (int i = 0; i < 100; ++i)
        {
            const auto relative = std::filesystem::path("Assets/Textures") / (std::to_string(i) + ".png");
            Require(index.Resolve(relative, one.at(i).get<std::string>()) == relative,
                    "all returned IDs resolve after reopen");
        }
        const auto asset = root / "Assets/Textures/0.png";
        const auto original = FileContents::Read(Engine::AssetDatabase::Sidecar(asset));
        auto settings = Engine::AssetDatabase::Read(asset);
        settings.scale = 2;
        Engine::AssetDatabase::Write(asset, settings, original);
        const auto external = FileContents::Read(Engine::AssetDatabase::Sidecar(asset));
        settings.scale = 3;
        bool rejected = false;
        try
        {
            Engine::AssetDatabase::Write(asset, settings, original);
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        Require(rejected && FileContents::Read(Engine::AssetDatabase::Sidecar(asset)) == external,
                "stale import settings cannot overwrite new settings");
        Require(Engine::AssetDatabase::Ensure(asset).id == one.at(0).get<std::string>() &&
                    FileContents::Read(Engine::AssetDatabase::Sidecar(asset)) == external,
                "Ensure preserves existing ID and settings");
        Child(L"--meta-gap", root).Wait(77);
        Require(!FileContents::Read(Engine::AssetDatabase::Sidecar(asset)), "metadata missing at crash checkpoint");
        Require(Engine::AssetDatabase::Ensure(asset).id == one.at(0).get<std::string>() &&
                    FileContents::Read(Engine::AssetDatabase::Sidecar(asset)) == external,
                "Ensure restores pending metadata before considering initial registration");
        const auto alias = root / "assets/textures/0.png";
        Require(Engine::AssetDatabase::Ensure(alias).id == one.at(0).get<std::string>(),
                "Windows path case preserves the save domain and ID");
        std::cout << "PASS: two-process metadata generation, stable reference IDs and stale import rejection\n";
    }
    void InvalidJournal(const std::filesystem::path& root)
    {
        Seed(root);
        Child(L"--mixed", root).Wait(77);
        std::filesystem::path manifest;
        for (const auto& entry : std::filesystem::directory_iterator(root / ".wp1-transactions"))
            if (entry.path().extension() == ".pending")
                manifest = entry.path() / "manifest.json";
        auto json = Engine::Json::parse(*FileContents::Read(manifest));
        json["entries"][1]["path"] = FileContents::Text(root.parent_path() / "outside.txt");
        FileContents::Write(manifest, json.dump());
        bool rejected = false;
        try
        {
            FileJournal::Recover(root);
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        Require(rejected && FileContents::Read(root / "scene.txt") == "new-scene" &&
                    !FileContents::Read(root.parent_path() / "outside.txt"),
                "invalid journal is rejected before restoring any file");
        std::cout << "PASS: invalid recovery paths preserve all snapshots and block recovery\n";
    }
    void Boundaries(const std::filesystem::path& root)
    {
        std::vector<FileJournal::Entry> excessive(1025);
        for (auto& entry : excessive)
            entry.path = root / "first/a.txt";
        bool rejected = false;
        try
        {
            FileJournal::Root(excessive);
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        Require(rejected && !std::filesystem::exists(root), "oversized journal rejected before preparation");
        std::filesystem::create_directories(root / "first");
        std::filesystem::create_directories(root / "second");
        const auto a = root / "first/a.txt", b = root / "second/b.txt";
        FileContents::Write(a, "A");
        FileContents::Write(b, "B");
        std::string error;
        const auto changes = std::vector<Editor::FileChange>{Editor::FileTransaction::Prepare(a, "A2"),
                                                             Editor::FileTransaction::Prepare(b, "B2")};
        Require(!Editor::FileTransaction::Apply(changes, true, error,
                                                []
                                                {
                                                    return true;
                                                }) &&
                    FileContents::Read(a) == "A" && FileContents::Read(b) == "B",
                "cross-domain save rejected before mutation");
        const auto absent = root / "first/absent.txt";
        const auto noOp = Editor::FileTransaction::Prepare(absent, {});
        Require(!Editor::FileTransaction::Apply({noOp}, true, error,
                                                [&]
                                                {
                                                    CreateForeign(absent);
                                                    return false;
                                                }) &&
                    FileContents::Read(absent) == "foreign-edit",
                "no-op plans never take ownership of subsequently created files");
        std::cout << "PASS: save domain boundaries and no-op ownership\n";
    }
}

int wmain(int argc, wchar_t** argv)
{
    try
    {
        if (argc >= 3)
            return RunChild(argc, argv);
        const auto root = std::filesystem::absolute("generated/tests/file-save-recovery") /
                          (std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
        PublicSaveApis(root / "public-apis");
        Crashes(root / "crashes");
        Conflicts(root / "conflicts");
        Metadata(root / "metadata");
        InvalidJournal(root / "invalid-journal");
        Boundaries(root / "boundaries");
        return 0;
    }
    catch (const std::exception& failure)
    {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
