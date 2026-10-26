#include "../../../Tools/ProjectTool/ScriptBuild.h"
#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace
{
    using namespace ProjectTool;

    void Require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    struct Handle final
    {
        HANDLE value = nullptr;
        ~Handle()
        {
            if (value && value != INVALID_HANDLE_VALUE)
                CloseHandle(value);
        }
    };

    class Worker final
    {
        Handle process_;

    public:
        Worker(const Path& executable, const std::vector<std::wstring>& arguments)
        {
            auto command = QuoteArgument(executable.wstring());
            for (const auto& argument : arguments)
                command += L" " + QuoteArgument(argument);
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION process{};
            Require(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                                   nullptr, nullptr, &startup, &process) != FALSE,
                    "Cannot start validation worker.");
            CloseHandle(process.hThread);
            process_.value = process.hProcess;
        }
        ~Worker()
        {
            if (WaitForSingleObject(process_.value, 0) == WAIT_TIMEOUT)
            {
                TerminateProcess(process_.value, ERROR_CANCELLED);
                WaitForSingleObject(process_.value, 5000);
            }
        }
        void Success()
        {
            Require(WaitForSingleObject(process_.value, 10000) == WAIT_OBJECT_0, "Validation worker timed out.");
            DWORD code = 1;
            Require(GetExitCodeProcess(process_.value, &code) && code == 0, "Validation worker failed.");
        }
        void Kill()
        {
            Require(TerminateProcess(process_.value, ERROR_CANCELLED) != FALSE, "Cannot abandon validation worker.");
            Require(WaitForSingleObject(process_.value, 5000) == WAIT_OBJECT_0, "Abandoned worker remained alive.");
        }
    };

    void WaitFile(const Path& path)
    {
        const auto started = GetTickCount64();
        while (!std::filesystem::is_regular_file(path))
        {
            Require(GetTickCount64() - started < 10000, "Worker marker was not created.");
            Sleep(20);
        }
    }

    int PublicationWorker(const Path& fixture, const std::string& name, bool expectTimeout)
    {
        std::unique_ptr<ScriptBuildSession> session;
        try
        {
            session = std::make_unique<ScriptBuildSession>(fixture, "Release", 1000);
        }
        catch (const std::exception& error)
        {
            if (expectTimeout && std::string(error.what()).starts_with("Timed out"))
                return 0;
            throw;
        }
        Require(!expectTimeout, "Concurrent build entered an owned session.");
        WriteText(fixture / (name + ".entered"), "entered");
        WaitFile(fixture / (name + ".release"));
        PublishScriptManifest(fixture / "module.json", {{"version", 1}, {"file", "owner.dll"}});
        return 0;
    }

    void ValidatePublication(const Path& executable, const Path& fixture)
    {
        Worker owner(executable, {L"--worker", fixture.wstring(), L"owner", L"0"});
        WaitFile(fixture / "owner.entered");
        Worker contender(executable, {L"--worker", (fixture / ".").wstring(), L"contender", L"1"});
        contender.Success();
        Require(!std::filesystem::exists(fixture / "contender.entered"), "Output publication was not serialized.");
        WriteText(fixture / "owner.release", "release");
        owner.Success();
        const auto manifest = fixture / "module.json";
        const auto before = Sha256(manifest);
        {
            Handle locked{CreateFileW(manifest.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL, nullptr)};
            Require(locked.value != INVALID_HANDLE_VALUE, "Cannot lock manifest for publication validation.");
            bool rejected = false;
            try
            {
                PublishScriptManifest(manifest, {{"file", "broken.dll"}});
            }
            catch (const std::exception&)
            {
                rejected = true;
            }
            Require(rejected && Sha256(manifest) == before, "Failed publication changed the old manifest.");
        }
        for (const auto& file : std::filesystem::directory_iterator(fixture))
            Require(file.path().extension() != ".tmp", "Failed publication leaked a temporary file.");
        Worker abandoned(executable, {L"--worker", fixture.wstring(), L"abandoned", L"0"});
        WaitFile(fixture / "abandoned.entered");
        abandoned.Kill();
        const ScriptBuildSession recovered(fixture / ".", "Release", 1000);
        PublishScriptManifest(manifest, {{"file", "recovered.dll"}});
        Require(ReadJson(manifest).at("file") == "recovered.dll", "Abandoned build recovery failed.");
    }

    void ValidateProcesses(const Path& executable, const Path& fixture)
    {
        Require(QuoteArgument(L"--validate-package") == L"--validate-package",
                "Simple switches must preserve the raw wWinMain command line.");
        const std::vector<std::wstring> arguments{L"",
                                                  L"with spaces",
                                                  L"trailing slash\\",
                                                  L"quote\"inside",
                                                  L"slashes\\\\\"quote",
                                                  L"日本語 空白\\",
                                                  L"%PATH% & | $()"};
        auto invocation = std::vector<std::wstring>{L"--arguments", (fixture / "arguments.json").wstring()};
        invocation.insert(invocation.end(), arguments.begin(), arguments.end());
        Require(RunProcess(executable, invocation, fixture, fixture / "arguments.log", 10000) == 0,
                "Argument worker failed.");
        const auto actual = ReadJson(fixture / "arguments.json");
        Require(actual.size() == arguments.size(), "Windows argument count changed.");
        for (size_t index = 0; index < arguments.size(); ++index)
            Require(actual.at(index) == Utf8(Path(arguments[index])), "Windows argument contents changed.");
        bool timedOut = false;
        try
        {
            RunProcess(executable, {L"--spawn-child", fixture.wstring()}, fixture, fixture / "timeout.log", 2000);
        }
        catch (const std::exception&)
        {
            timedOut = true;
        }
        Require(timedOut, "Long-running subprocess was not cancelled.");
        WaitFile(fixture / "descendant.json");
        const auto id = ReadJson(fixture / "descendant.json").at("pid").get<DWORD>();
        Handle descendant{OpenProcess(SYNCHRONIZE, FALSE, id)};
        Require(!descendant.value || WaitForSingleObject(descendant.value, 5000) == WAIT_OBJECT_0,
                "Cancelled subprocess left its descendant running.");
    }

    void ValidateSources(const Path& engine, const Path& fixture)
    {
        Options options;
        options.engine = engine;
        options.content = fixture / "content";
        options.output = fixture / "output";
        options.work = fixture;
        options.configuration = "Release";
        options.runId = "empty";
        std::filesystem::create_directories(options.content);
        BuildScripts(options);
        const auto manifest = options.output / "Assets/Scripts/Bin/Release/module.json";
        const auto before = Sha256(manifest);
        Require(ReadJson(manifest).at("file") == "", "Empty source set did not publish an empty module.");
        Require(ReadJson(fixture / "generated/script-builds/empty.json").at("success") == true,
                "Empty module build result was not published.");
        WriteText(options.content / "Assets/Scripts/one/Same.cpp", "");
        WriteText(options.content / "Assets/Scripts/two/same.cpp", "");
        options.runId = "duplicate";
        bool rejected = false;
        try
        {
            BuildScripts(options);
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        Require(rejected && Sha256(manifest) == before, "Duplicate source names changed the public module.");
        const auto failure = ReadJson(fixture / "generated/script-builds/duplicate.json");
        Require(failure.at("success") == false && failure.at("error").is_string(),
                "Invalid source diagnostics were not published.");
        WriteJsonAtomic(options.content / "WP1.project.json", {{"version", 1},
                                                               {"name", "Other"},
                                                               {"project", "Other.vcxproj"},
                                                               {"solution", "Other.slnx"},
                                                               {"engineRoot", Utf8(fixture / "another-engine")}});
        options.runId = "wrong-engine";
        rejected = false;
        try
        {
            BuildScripts(options);
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        const auto mismatch = ReadJson(fixture / "generated/script-builds/wrong-engine.json");
        Require(rejected && mismatch.at("success") == false &&
                    mismatch.at("error").get<std::string>().find("different engine") != std::string::npos &&
                    Sha256(manifest) == before,
                "Engine mismatch did not publish diagnostics while preserving the public module.");
    }
}

int wmain(int argc, wchar_t** argv)
{
    try
    {
        using namespace ProjectTool;
        std::array<wchar_t, 32768> executableText{};
        const auto length =
            GetModuleFileNameW(nullptr, executableText.data(), static_cast<DWORD>(executableText.size()));
        Require(length && length < executableText.size(), "Cannot locate validation executable.");
        const Path executable(executableText.data());
        if (argc >= 2 && std::wstring_view(argv[1]) == L"--worker")
        {
            Require(argc == 5, "Invalid publication worker arguments.");
            return PublicationWorker(Path(argv[2]), Utf8(Path(argv[3])), std::wstring_view(argv[4]) == L"1");
        }
        if (argc >= 2 && std::wstring_view(argv[1]) == L"--arguments")
        {
            Require(argc >= 3, "Missing argument output path.");
            Json arguments = Json::array();
            for (int index = 3; index < argc; ++index)
                arguments.push_back(Utf8(Path(argv[index])));
            WriteJsonAtomic(Path(argv[2]), arguments);
            return 0;
        }
        if (argc == 3 && std::wstring_view(argv[1]) == L"--descendant")
        {
            WriteJsonAtomic(Path(argv[2]) / "descendant.json", {{"pid", GetCurrentProcessId()}});
            Sleep(20000);
            return 0;
        }
        if (argc == 3 && std::wstring_view(argv[1]) == L"--spawn-child")
        {
            const Path fixture(argv[2]);
            return RunProcess(executable, {L"--descendant", fixture.wstring()}, fixture, fixture / "descendant.log");
        }
        Require(argc == 2, "Pass the engine repository root.");
        const Path engine = std::filesystem::absolute(Path(argv[1]));
        const auto fixture = engine / "generated/tests/project-tool" / NewId();
        std::filesystem::create_directories(fixture);
        Require(Sha256Text("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                "SHA-256 text fingerprint changed.");
        WriteText(fixture / "hash.txt", "abc");
        Require(Sha256(fixture / "hash.txt") == Sha256Text("abc"), "SHA-256 file fingerprint changed.");
        ValidatePublication(executable, fixture);
        ValidateProcesses(executable, fixture);
        ValidateSources(engine, fixture);
        std::cout
            << "PASS: publication exclusion, atomic replacement, abandoned recovery, argument quoting, descendant cancellation and diagnostics\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
