#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <map>

namespace
{
    std::function<void(const std::string&, const std::filesystem::path&)> checkpoint;
    bool denyCompletion = false, denyManifest = false, denyAbort = false, denyAcceptance = false;

    HANDLE WINAPI ObserveCreate(LPCWSTR name, DWORD access, DWORD sharing, LPSECURITY_ATTRIBUTES security,
                                DWORD creation, DWORD flags, HANDLE templateFile)
    {
        if (denyManifest && creation == OPEN_EXISTING && std::filesystem::path(name).filename() == "manifest.json")
        {
            SetLastError(ERROR_ACCESS_DENIED);
            return INVALID_HANDLE_VALUE;
        }
        return ::CreateFileW(name, access, sharing, security, creation, flags, templateFile);
    }

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
        const auto name = std::filesystem::path(to).filename();
        if ((denyCompletion && name == "completed") || (denyAbort && name == "aborted") ||
            (denyAcceptance && name == "accepted"))
        {
            SetLastError(ERROR_ACCESS_DENIED);
            return FALSE;
        }
        const auto result = ::MoveFileExW(from, to, flags);
        const auto error = GetLastError();
        if (result && checkpoint)
            checkpoint("publish", to);
        SetLastError(error);
        return result;
    }
}

// 実APIの成功後だけで停止し、管理対象の保存実装をそのまま検証します。
#define CreateFileW ObserveCreate
#define SetFileInformationByHandle ObserveInformation
#define MoveFileExW ObserveMove
#include "../Editor/src/FileTransaction.h"
#include <Engine/Assets/AssetDatabase.h>
#include <SceneRuntime/ProjectSettings.h>
#undef CreateFileW
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
            : root_(root), mode_(mode)
        {
            auto command = L"\"" + (executable.empty() ? Executable() : executable).native() + L"\" " + mode + L" \"" +
                           root.native() + L"\" " + extra;
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            const auto started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                                                nullptr, nullptr, &startup, &process_);
            const auto error = GetLastError();
            Require(started != FALSE, "start child: " + FileContents::Text(root_) + ", error " + std::to_string(error));
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
        void Wait(DWORD expected = 0, std::optional<DWORD> alternative = {})
        {
            Require(WaitForSingleObject(process_.hProcess, 30000) == WAIT_OBJECT_0,
                    "child timeout: " + FileContents::Text(mode_) + " " + FileContents::Text(root_));
            DWORD code = 0;
            const bool exited = GetExitCodeProcess(process_.hProcess, &code) != FALSE;
            if (!exited || (code != expected && (!alternative || code != *alternative)))
            {
                auto diagnostic =
                    FileContents::Read(root_ / ("child-error-" + std::to_string(process_.dwProcessId) + ".log"));
                if (!diagnostic)
                    diagnostic = FileContents::Read(root_ / "validation-error.log");
                throw std::runtime_error("child exit " + std::to_string(code) + ", expected " +
                                         std::to_string(expected) + ": " + FileContents::Text(mode_) + " " +
                                         FileContents::Text(root_) + "\n" + diagnostic.value_or("No child diagnostic"));
            }
        }

    private:
        PROCESS_INFORMATION process_{};
        std::filesystem::path root_, mode_;
    };
    std::vector<Editor::FileChange> Plans(const std::filesystem::path& root)
    {
        return {Editor::FileTransaction::Prepare(root / "scene.txt", "new-scene"),
                Editor::FileTransaction::Prepare(root / "material.txt", "new-material")};
    }
    void MoveLegacyJournal(const std::filesystem::path& source, const std::filesystem::path& destination,
                           DWORD timeout = 1000)
    {
        // 旧形式の配置だけを再試行し、保存・復旧本体の失敗を隠さないようにします。
        const auto deadline = GetTickCount64() + timeout;
        while (true)
        {
            DWORD error = ERROR_ALREADY_EXISTS;
            if (!std::filesystem::exists(destination))
            {
                if (MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH))
                    return;
                error = GetLastError();
            }
            if ((error == ERROR_ACCESS_DENIED || error == ERROR_SHARING_VIOLATION) && GetTickCount64() < deadline)
            {
                Sleep(5);
                continue;
            }
            throw std::runtime_error("legacy journal relocation failed (error " + std::to_string(error) +
                                     "): " + FileContents::Text(source) + " -> " + FileContents::Text(destination));
        }
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
        if (mode == L"--hold-journal")
        {
            Require(argc == 5, "journal holder events");
            auto directory =
                CreateFileW((root / "manifest.json").c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            Require(directory != INVALID_HANDLE_VALUE, "hold journal without delete sharing");
            auto ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, argv[3]);
            auto release = OpenEventW(SYNCHRONIZE, FALSE, argv[4]);
            Require(ready && release, "open journal holder events");
            SetEvent(ready);
            Require(WaitForSingleObject(release, 10000) == WAIT_OBJECT_0, "journal holder release");
            CloseHandle(directory);
            CloseHandle(ready);
            CloseHandle(release);
            return 0;
        }
        if (mode == L"--foreign")
        {
            CreateForeign(root / "scene.txt");
            return 0;
        }
        if (mode == L"--recover-index")
        {
            Require(argc == 4, "recovery index argument");
            int count = 0;
            const int target = std::stoi(argv[3]);
            checkpoint = [&](const std::string& operation, const std::filesystem::path&)
            {
                if ((operation == "rename" || operation == "publish") && ++count == target)
                    TerminateProcess(GetCurrentProcess(), 77);
            };
            FileJournal::Recover(root);
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
        if (mode == L"--nested-assets" || mode == L"--nested-shaders")
            plans = Plans(root / "Assets/Models/Package" / (mode == L"--nested-assets" ? "Assets" : "Shaders"));
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
        if (mode == L"--meta-gap" || mode == L"--late-meta-gap")
        {
            const auto asset = root / (mode == L"--late-meta-gap" ? std::filesystem::path(argv[3])
                                                                  : std::filesystem::path("Assets/Textures/0.png"));
            auto metadata = Engine::AssetDatabase::Read(asset);
            metadata.scale = 5;
            plans = {Editor::FileTransaction::Prepare(Engine::AssetDatabase::Sidecar(asset),
                                                      Engine::AssetDatabase::SerializeMetadata(metadata))};
        }
        int renames = 0;
        if (mode == L"--abort-denied")
            denyAbort = true;
        checkpoint = [&](const std::string& operation, const std::filesystem::path& to)
        {
            if (operation == "rename")
            {
                ++renames;
                if (mode == L"--gap" || mode == L"--api-gap" || mode == L"--project-gap" || mode == L"--meta-gap" ||
                    mode == L"--nested-assets" || mode == L"--nested-shaders" || mode == L"--late-meta-gap" ||
                    mode == L"--create-delete" || (mode == L"--mixed" && renames == 2) ||
                    (mode == L"--second-gap" && renames == 3) || (mode == L"--all-installed" && renames == 4))
                    TerminateProcess(GetCurrentProcess(), 77);
                if (mode == L"--rollback-gap" && renames >= 5)
                    TerminateProcess(GetCurrentProcess(), 77);
            }
            if ((mode == L"--manifest" && operation == "publish" && to.filename() == "manifest.json") ||
                ((mode == L"--committed" || mode == L"--api-committed" || mode == L"--create-delete-commit") &&
                 operation == "publish" && to.filename() == "committed") ||
                (mode == L"--completed" && operation == "publish" && to.filename() == "completed") ||
                (mode == L"--abort-record" && operation == "publish" && to.filename() == "aborted") ||
                (mode == L"--abort-denied" && operation == "delete") || (mode == L"--cleanup" && operation == "delete"))
                TerminateProcess(GetCurrentProcess(), 77);
        };
        std::string error;
        Editor::FileTransaction::Apply(plans, true, error,
                                       [&]
                                       {
                                           return mode != L"--rollback-gap" && mode != L"--abort-record" &&
                                                  mode != L"--abort-denied";
                                       });
        throw std::runtime_error("save checkpoint not reached: " + error);
    }
    void FixtureMoves(const std::filesystem::path& root)
    {
        for (const bool exhaust : {false, true})
        {
            const auto directory = root / (exhaust ? "deadline" : "temporary-lock");
            Seed(directory);
            Child(L"--gap", directory).Wait(77);
            const auto source = std::filesystem::directory_iterator(directory / ".wp1-transactions")->path();
            const auto manifestPath = source / "manifest.json";
            auto manifest = Engine::Json::parse(*FileContents::Read(manifestPath));
            manifest["version"] = 2;
            FileContents::Write(manifestPath, manifest.dump());
            const auto parent = directory / "Assets/Models/Package/.wp1-transactions";
            std::filesystem::create_directories(parent);
            const auto destination = parent / source.filename();
            const auto snapshot = [](const std::filesystem::path& folder)
            {
                std::map<std::filesystem::path, std::string> contents;
                for (const auto& entry : std::filesystem::directory_iterator(folder))
                    contents.emplace(entry.path().filename(), *FileContents::Read(entry.path()));
                return contents;
            };
            const auto original = snapshot(source);
            const auto event =
                L"Local\\WP1.JournalMove." + std::to_wstring(GetCurrentProcessId()) + std::to_wstring(GetTickCount64());
            auto ready = CreateEventW(nullptr, TRUE, FALSE, (event + L".ready").c_str());
            auto release = CreateEventW(nullptr, TRUE, FALSE, (event + L".release").c_str());
            Require(ready && release, "create journal move events");
            {
                Child holder(L"--hold-journal", source, event + L".ready " + event + L".release");
                Require(WaitForSingleObject(ready, 10000) == WAIT_OBJECT_0, "journal lock ready");
                bool blocked = false;
                try
                {
                    std::filesystem::rename(source, destination);
                }
                catch (const std::filesystem::filesystem_error& error)
                {
                    blocked =
                        error.code().value() == ERROR_ACCESS_DENIED || error.code().value() == ERROR_SHARING_VIOLATION;
                }
                Require(blocked && snapshot(source) == original && !std::filesystem::exists(destination),
                        "single-shot legacy relocation reproduces access denial without changing snapshots");
                if (exhaust)
                {
                    std::string error;
                    try
                    {
                        MoveLegacyJournal(source, destination, 60);
                    }
                    catch (const std::exception& failure)
                    {
                        error = failure.what();
                    }
                    Require(error.find("error ") != std::string::npos &&
                                error.find(FileContents::Text(source)) != std::string::npos &&
                                snapshot(source) == original && !std::filesystem::exists(destination),
                            "exhausted relocation reports the OS error and retains all data");
                    SetEvent(release);
                }
                else
                {
                    std::jthread unlock(
                        [release]
                        {
                            Sleep(50);
                            SetEvent(release);
                        });
                    MoveLegacyJournal(source, destination);
                }
                holder.Wait();
            }
            CloseHandle(ready);
            CloseHandle(release);
            if (exhaust)
            {
                std::filesystem::create_directory(destination);
                FileContents::Write(destination / "guard", "existing-destination");
                bool refused = false;
                try
                {
                    MoveLegacyJournal(source, destination);
                }
                catch (const std::exception&)
                {
                    refused = true;
                }
                Require(refused && snapshot(source) == original &&
                            FileContents::Read(destination / "guard") == "existing-destination",
                        "existing destination is never overwritten");
                FileContents::Remove(destination / "guard");
                std::filesystem::remove(destination);
                MoveLegacyJournal(source, destination);
            }
            Require(!std::filesystem::exists(source) && snapshot(destination) == original,
                    "relocation retains every journal file");
            FileJournal::Recover(directory);
            CheckGeneration(directory, false);
        }
        std::cout << "PASS: legacy fixture relocation retries temporary locks and preserves data on failure\n";
    }
    void Diagnostics(const std::filesystem::path& root)
    {
        const auto executable =
            Executable().parent_path().parent_path() / "FileSaveApiValidation/FileSaveApiValidation.exe";
        for (const bool suite : {false, true})
        {
            const auto directory = root / (suite ? "suite" : "api");
            std::filesystem::create_directories(directory);
            std::string error;
            try
            {
                Child(suite ? L"--suite-diagnostic" : L"--scan-project", directory, L"",
                      suite ? std::filesystem::path{} : executable)
                    .Wait();
            }
            catch (const std::exception& failure)
            {
                error = failure.what();
            }
            Require(error.find("child exit 1") != std::string::npos &&
                        error.find(FileContents::Text(directory)) != std::string::npos &&
                        error.find(suite ? "injected suite diagnostic" : "project scan failed:") != std::string::npos,
                    "child cause, mode and case survive API and suite boundaries");
        }
        std::cout << "PASS: child API and suite failures retain detailed diagnostics\n";
    }
    void NestedDomains(const std::filesystem::path& root)
    {
        const auto executable =
            Executable().parent_path().parent_path() / "FileSaveApiValidation/FileSaveApiValidation.exe";
        for (const bool legacy : {false, true})
            for (const auto* mode : {L"--nested-assets", L"--nested-shaders"})
            {
                const auto directory = root / (std::wstring(mode) + (legacy ? L"-legacy" : L"-current"));
                Seed(directory);
                const auto nested = directory / "Assets/Models/Package" /
                                    (std::wstring(mode) == L"--nested-assets" ? "Assets" : "Shaders");
                Seed(nested);
                const auto descriptor =
                    Engine::Json{{"version", 1},
                                 {"name", "Recovery"},
                                 {"engineRoot", FileContents::Text(Engine::ProjectContext::SourceEngineRoot())},
                                 {"project", "Recovery.vcxproj"},
                                 {"solution", "Recovery.slnx"},
                                 {"projectId", "0123456789abcdef0123456789abcdef"}};
                FileContents::Write(directory / "WP1.project.json", descriptor.dump());
                FileContents::Write(directory / "Recovery.vcxproj", "<Project />");
                Child(L"--seed-api", directory, L"", executable).Wait();
                FileContents::Write(directory / "Assets/Project.json",
                                    Engine::Json{{"title", "Recovery"},
                                                 {"startupScene", "Assets/Scenes/scene.json"},
                                                 {"width", 1280},
                                                 {"height", 720}}
                                        .dump());
                Require(Engine::FileSaveLock::Root(nested / "scene.txt") == directory,
                        "descriptor owns nested Assets and Shaders");
                Require(Engine::AssetDatabase::Root(nested / "scene.txt") == directory,
                        "asset references share the save domain");
                Child(mode, directory).Wait(77);
                if (legacy)
                {
                    const auto oldRoot = directory / "Assets/Models/Package/.wp1-transactions";
                    std::filesystem::create_directories(oldRoot);
                    for (const auto& entry : std::filesystem::directory_iterator(directory / ".wp1-transactions"))
                    {
                        const auto manifest = entry.path() / "manifest.json";
                        auto json = Engine::Json::parse(*FileContents::Read(manifest));
                        json["version"] = 2;
                        FileContents::Write(manifest, json.dump());
                        MoveLegacyJournal(entry.path(), oldRoot / entry.path().filename());
                    }
                }
                Child(legacy ? L"--scan-project" : L"--open-project", directory, L"", executable).Wait();
                Require(FileContents::Read(nested / "scene.txt") == "old-scene" &&
                            FileContents::Read(nested / "material.txt") == "old-material",
                        "project open/scan recovers current and legacy nested journals");
                FileJournal::Recover(directory);
            }
        const auto bare = root / "bare/Assets/Models/Package/Assets/file.txt";
        std::filesystem::create_directories(bare.parent_path());
        Require(Engine::FileSaveLock::Root(bare) == root / "bare", "marker-free asset roots remain one domain");
        const auto packaged = root / "packaged";
        std::filesystem::create_directories(packaged / "Assets/Models/Assets");
        FileContents::Write(packaged / "WP1.runtime.json", "{}");
        Require(Engine::FileSaveLock::Root(packaged / "Assets/Models/Assets/file.txt") == packaged,
                "runtime descriptor also owns nested folders");
        std::cout << "PASS: nested Assets/Shaders, project Open/Scan and legacy journal discovery\n";
    }
    void Cancellation(const std::filesystem::path& root)
    {
        for (const auto* name : {L"completed", L"committed"})
            for (const bool throws : {false, true})
            {
                const auto directory = root / (std::wstring(name) + (throws ? L"-throw" : L"-reject"));
                Seed(directory);
                HANDLE marker = INVALID_HANDLE_VALUE;
                std::string error;
                const bool saved = Editor::FileTransaction::Apply(
                    Plans(directory), true, error,
                    [&]() -> bool
                    {
                        for (const auto& entry : std::filesystem::directory_iterator(directory / ".wp1-transactions"))
                            marker = CreateFileW((entry.path() / name).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                        Require(marker != INVALID_HANDLE_VALUE, "lock completion or commit marker");
                        if (throws)
                            throw std::runtime_error("document refused");
                        return false;
                    });
                CloseHandle(marker);
                Require(!saved && !error.empty(), "rejected document is reported");
                FileJournal::Recover(directory);
                FileJournal::Recover(directory);
                CheckGeneration(directory, false);
            }
        for (const bool accepted : {false, true})
        {
            const auto directory = root / (accepted ? "acceptance-unrecorded" : "abort-unrecorded");
            Seed(directory);
            std::string error;
            const bool saved = Editor::FileTransaction::Apply(Plans(directory), true, error,
                                                              [&]
                                                              {
                                                                  denyAbort = !accepted;
                                                                  denyAcceptance = accepted;
                                                                  return accepted;
                                                              });
            denyAbort = denyAcceptance = false;
            Require(saved == accepted && !error.empty(), "unrecorded outcome is diagnosed");
            FileContents::Write(directory / "scene.txt", "later-edit");
            FileContents::Remove(directory / "material.txt");
            FileJournal::Recover(directory);
            FileJournal::Recover(directory);
            Require(FileContents::Read(directory / "scene.txt") == "later-edit" &&
                        !FileContents::Read(directory / "material.txt"),
                    "uncertain cleanup preserves later edits");
            Require(Archived(directory, "old-scene") && Archived(directory, "new-scene") &&
                        Archived(directory, "old-material") && Archived(directory, "new-material"),
                    "all generations survive unrecorded outcomes");
        }
        for (const auto* mode : {L"--abort-record", L"--abort-denied"})
            for (int index = 1; index <= 12; ++index)
            {
                const auto directory = root / (std::wstring(mode) + L"-" + std::to_wstring(index));
                Seed(directory);
                Child(mode, directory).Wait(77);
                Child(L"--recover-index", directory, std::to_wstring(index)).Wait(0, 77);
                Child(L"--recover", directory).Wait();
                Child(L"--recover", directory).Wait();
                if (std::wstring(mode) == L"--abort-record")
                    CheckGeneration(directory, false);
                else
                {
                    CheckGeneration(directory, true);
                    Require(Archived(directory, "old-scene") && Archived(directory, "new-scene"),
                            "uncertain cancellation survives recovery crashes");
                }
            }
        std::cout << "PASS: durable cancellation, locked markers, uncertain outcomes and 24 recovery restart cases\n";
    }
    void Completion(const std::filesystem::path& root)
    {
        const auto refused = root / "completion-failure";
        Seed(refused);
        const auto changes = Plans(refused);
        std::string error;
        bool callback = false;
        denyCompletion = true;
        const bool accepted = Editor::FileTransaction::Apply(changes, true, error,
                                                             [&]
                                                             {
                                                                 callback = true;
                                                                 return true;
                                                             });
        denyCompletion = false;
        Require(!accepted && !callback && !error.empty(), "completion failure precedes document acceptance");
        FileJournal::Recover(refused);
        CheckGeneration(refused, false);

        const auto cleanup = root / "cleanup-failure";
        Seed(cleanup);
        Require(Editor::FileTransaction::Apply(Plans(cleanup), true, error,
                                               [&]
                                               {
                                                   denyManifest = true;
                                                   return true;
                                               }),
                "cleanup failure follows a completed save");
        denyManifest = false;
        Require(error.find("掃除") != std::string::npos, "cleanup failure is diagnosed");
        FileContents::Write(cleanup / "scene.txt", "saved-after-success");
        FileContents::Remove(cleanup / "material.txt");
        FileJournal::Recover(cleanup);
        Require(FileContents::Read(cleanup / "scene.txt") == "saved-after-success" &&
                    !FileContents::Read(cleanup / "material.txt"),
                "later edits and deletion survive cleanup retry");

        const auto completed = root / "completed-crash";
        Seed(completed);
        Child(L"--completed", completed).Wait(77);
        FileContents::Write(completed / "scene.txt", "external-after-completion");
        FileContents::Remove(completed / "material.txt");
        FileJournal::Recover(completed);
        Require(FileContents::Read(completed / "scene.txt") == "external-after-completion" &&
                    !FileContents::Read(completed / "material.txt"),
                "completed crash does not replay older files");

        for (const int version : {2, 3})
            for (const auto* mode : {L"--committed", L"--completed"})
            {
                if (version == 2 && std::wstring(mode) == L"--completed")
                    continue;
                const auto directory = root / (L"legacy-" + std::to_wstring(version) + mode);
                Seed(directory);
                Child(mode, directory).Wait(77);
                for (const auto& entry : std::filesystem::directory_iterator(directory / ".wp1-transactions"))
                {
                    const auto path = entry.path() / "manifest.json";
                    auto json = Engine::Json::parse(*FileContents::Read(path));
                    json["version"] = version;
                    FileContents::Write(path, json.dump());
                }
                FileJournal::Recover(directory);
                FileJournal::Recover(directory);
                CheckGeneration(directory, true);
                Require(Archived(directory, "old-scene") && Archived(directory, "new-scene"),
                        "legacy journals without receipts preserve both generations");
            }
        const auto legacy = root / "version-two";
        Seed(legacy);
        Child(L"--mixed", legacy).Wait(77);
        for (const auto& entry : std::filesystem::directory_iterator(legacy / ".wp1-transactions"))
        {
            const auto path = entry.path() / "manifest.json";
            auto json = Engine::Json::parse(*FileContents::Read(path));
            json["version"] = 2;
            FileContents::Write(path, json.dump());
        }
        FileJournal::Recover(legacy);
        CheckGeneration(legacy, false);
        for (const auto* mode : {L"--mixed", L"--committed", L"--create-delete-commit", L"--completed"})
            for (int index = 1; index <= 12; ++index)
            {
                const auto directory = root / (std::wstring(mode) + L"-" + std::to_wstring(index));
                Seed(directory);
                Child(mode, directory).Wait(77);
                Child(L"--recover-index", directory, std::to_wstring(index)).Wait(77, 0);
                FileJournal::Recover(directory);
                FileJournal::Recover(directory);
                if (std::wstring(mode) == L"--create-delete-commit")
                    Require(FileContents::Read(directory / "created.bin") == std::string("\xff\0new", 5) &&
                                !FileContents::Read(directory / "scene.txt"),
                            "restarted creation/deletion recovery");
                else
                    CheckGeneration(directory, std::wstring(mode) != L"--mixed");
            }
        std::cout << "PASS: durable completion, cleanup retry, version 2 and 48 recovery restart cases\n";
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
    void LateLegacy(const std::filesystem::path& root)
    {
        for (const auto* folder : {"Assets", "Shaders"})
            for (const auto* operation : {"ensure", "save", "apply", "settings-load", "settings-save"})
            {
                const auto directory = root / folder / operation;
                Seed(directory);
                const auto relative = std::filesystem::path(folder) / "Package/Assets/0.png";
                const auto asset = directory / relative;
                std::filesystem::create_directories(asset.parent_path());
                std::filesystem::create_directories(directory / "Assets/Scenes");
                FileContents::Write(asset, "image");
                FileContents::Write(directory / "Assets/Scenes/Empty.json", "{}");
                FileContents::Write(
                    directory / "WP1.project.json",
                    Engine::Json{{"version", 1},
                                 {"name", "Legacy"},
                                 {"engineRoot", FileContents::Text(Engine::ProjectContext::SourceEngineRoot())},
                                 {"project", "Legacy.vcxproj"},
                                 {"solution", "Legacy.slnx"}}
                        .dump());
                FileContents::Write(directory / "Assets/Project.json",
                                    Engine::Json{{"title", "Legacy"},
                                                 {"startupScene", "Assets/Scenes/Empty.json"},
                                                 {"width", 1280},
                                                 {"height", 720}}
                                        .dump());
                const auto original = Engine::AssetDatabase::Ensure(asset).id;
                auto settings = SceneRuntime::ProjectSettings::Load(directory);
                const auto plans = Plans(directory);
                FileJournal::Recover(directory);
                Child(L"--late-meta-gap", directory, L"\"" + relative.native() + L"\"").Wait(77);
                const auto legacy = asset.parent_path().parent_path() / ".wp1-transactions";
                std::filesystem::create_directories(legacy);
                for (const auto& entry : std::filesystem::directory_iterator(directory / ".wp1-transactions"))
                {
                    const auto path = entry.path() / "manifest.json";
                    auto manifest = Engine::Json::parse(*FileContents::Read(path));
                    manifest["version"] = 2;
                    FileContents::Write(path, manifest.dump());
                    MoveLegacyJournal(entry.path(), legacy / entry.path().filename());
                }
                Require(!FileContents::Read(Engine::AssetDatabase::Sidecar(asset)), "late journal has missing sidecar");
                const auto check = [&]
                {
                    Require(Engine::AssetDatabase::Read(asset).id == original,
                            "late legacy journal recovered before use");
                };
                const std::string action = operation;
                if (action == "ensure")
                    Require(Engine::AssetDatabase::Ensure(asset).id == original, "late journal never mints a new ID");
                else if (action == "save")
                    Engine::FileReplacement::Save(directory / "scene.txt",
                                                  [&]
                                                  {
                                                      check();
                                                      return std::string("saved-scene");
                                                  });
                else if (action == "apply")
                {
                    std::string error;
                    Require(Editor::FileTransaction::Apply(plans, true, error,
                                                           [&]
                                                           {
                                                               check();
                                                               return true;
                                                           }),
                            "late journal recovered before multi-file acceptance");
                }
                else if (action == "settings-load")
                    static_cast<void>(SceneRuntime::ProjectSettings::Load(directory));
                else
                {
                    settings.width = 1920;
                    settings.Save(directory);
                }
                check();
                FileJournal::Recover(directory);
                check();
                Engine::AssetDatabase index(directory);
                Require(index.Resolve(relative, original) == relative, "returned ID resolves after reopening");
            }
        std::cout << "PASS: late version 2 journals in Assets/Shaders recovered by all five entry points\n";
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
    std::filesystem::path root;
    const bool suite =
        argc >= 3 && (std::wstring(argv[1]) == L"--suite" || std::wstring(argv[1]) == L"--suite-diagnostic");
    try
    {
        if (argc >= 3 && !suite)
            return RunChild(argc, argv);
        root = suite ? std::filesystem::absolute(argv[2])
                     : std::filesystem::absolute("generated/tests/file-save-recovery") /
                           (std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
        std::filesystem::create_directories(root);
        if (suite && std::wstring(argv[1]) == L"--suite-diagnostic")
            throw std::runtime_error("injected suite diagnostic");
        Diagnostics(root / "diagnostics");
        FixtureMoves(root / "fixture-moves");
        PublicSaveApis(root / "public-apis");
        Completion(root / "completion");
        Cancellation(root / "cancellation");
        LateLegacy(root / "late-legacy");
        NestedDomains(root / "nested-domains");
        Crashes(root / "crashes");
        Conflicts(root / "conflicts");
        Metadata(root / "metadata");
        InvalidJournal(root / "invalid-journal");
        Boundaries(root / "boundaries");
        return 0;
    }
    catch (const std::exception& failure)
    {
        if (!root.empty() || argc >= 3)
        {
            // 非表示の子プロセスでも、失敗したOS操作を検証結果へ残します。
            const auto diagnosticPath =
                root.empty()
                    ? std::filesystem::path(argv[2]) / ("child-error-" + std::to_string(GetCurrentProcessId()) + ".log")
                    : root / "validation-error.log";
            std::ofstream diagnostic(diagnosticPath);
            diagnostic << failure.what() << '\n';
        }
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
