#include "ToolCommon.h"
#include <bcrypt.h>
#include <objbase.h>
#include <array>
#include <fstream>
#include <stdexcept>
#include <limits>

namespace ProjectTool
{
    namespace
    {
        struct Hash final
        {
            BCRYPT_ALG_HANDLE algorithm = nullptr;
            BCRYPT_HASH_HANDLE hash = nullptr;
            Hash()
            {
                if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
                    throw std::runtime_error("Cannot open SHA-256 provider.");
                if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0)
                {
                    BCryptCloseAlgorithmProvider(algorithm, 0);
                    throw std::runtime_error("Cannot create SHA-256 hash.");
                }
            }
            ~Hash()
            {
                BCryptDestroyHash(hash);
                BCryptCloseAlgorithmProvider(algorithm, 0);
            }
            void Add(const char* data, size_t size)
            {
                while (size)
                {
                    const auto count = static_cast<ULONG>((std::min)(size, size_t{1048576}));
                    if (BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(data)), count, 0) < 0)
                        throw std::runtime_error("Cannot hash input.");
                    data += count;
                    size -= count;
                }
            }
            std::string Finish()
            {
                std::array<unsigned char, 32> bytes{};
                if (BCryptFinishHash(hash, bytes.data(), static_cast<ULONG>(bytes.size()), 0) < 0)
                    throw std::runtime_error("Cannot finish SHA-256 hash.");
                constexpr char Hex[] = "0123456789abcdef";
                std::string result;
                for (const auto byte : bytes)
                {
                    result += Hex[byte >> 4];
                    result += Hex[byte & 15];
                }
                return result;
            }
        };
    }

    std::string Utf8(const Path& path)
    {
        const auto text = path.u8string();
        return {reinterpret_cast<const char*>(text.data()), text.size()};
    }

    std::wstring Wide(std::string_view text)
    {
        if (text.empty())
            return {};
        if (text.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
            throw std::runtime_error("UTF-8 argument is too long.");
        const auto length =
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
        if (!length)
            throw std::runtime_error("Invalid UTF-8 argument.");
        std::wstring result(static_cast<size_t>(length), L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(),
                            length);
        return result;
    }

    Path FromUtf8(std::string_view text)
    {
        return Path(Wide(text));
    }

    Json ReadJson(const Path& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
            throw std::runtime_error("Cannot read JSON: " + Utf8(path));
        return Json::parse(input);
    }

    void WriteText(const Path& path, std::string_view text)
    {
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        output.close();
        if (!output)
            throw std::runtime_error("Cannot write file: " + Utf8(path));
    }

    void WriteJsonAtomic(const Path& path, const Json& value)
    {
        const Path temporary = path.wstring() + L"." + Wide(NewId()) + L".tmp";
        try
        {
            WriteText(temporary, value.dump(2) + "\r\n");
            const auto file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                          FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE)
                throw std::runtime_error("Cannot open temporary JSON for flushing.");
            const bool flushed = FlushFileBuffers(file) != FALSE;
            CloseHandle(file);
            if (!flushed)
                throw std::runtime_error("Cannot flush temporary JSON.");
            if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("Cannot publish file: " + Utf8(path));
        }
        catch (...)
        {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            throw;
        }
    }

    std::string Sha256(const Path& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
            throw std::runtime_error("Cannot hash file: " + Utf8(path));
        Hash hash;
        std::array<char, 65536> buffer{};
        while (input.read(buffer.data(), buffer.size()) || input.gcount())
            hash.Add(buffer.data(), static_cast<size_t>(input.gcount()));
        if (!input.eof())
            throw std::runtime_error("Cannot read file while hashing: " + Utf8(path));
        return hash.Finish();
    }

    std::string Sha256Text(std::string_view text)
    {
        Hash hash;
        hash.Add(text.data(), text.size());
        return hash.Finish();
    }

    std::string NewId()
    {
        GUID guid{};
        if (FAILED(CoCreateGuid(&guid)))
            throw std::runtime_error("Cannot create build identifier.");
        std::array<wchar_t, 40> text{};
        StringFromGUID2(guid, text.data(), static_cast<int>(text.size()));
        std::string result;
        for (const auto character : text)
            if ((character >= L'0' && character <= L'9') || (character >= L'a' && character <= L'f') ||
                (character >= L'A' && character <= L'F'))
                result += static_cast<char>(character >= L'A' && character <= L'F' ? character + 32 : character);
        return result;
    }

    std::string XmlEscape(std::string_view text)
    {
        std::string result;
        for (const auto character : text)
        {
            switch (character)
            {
            case '&':
                result += "&amp;";
                break;
            case '<':
                result += "&lt;";
                break;
            case '>':
                result += "&gt;";
                break;
            case '"':
                result += "&quot;";
                break;
            case '\'':
                result += "&apos;";
                break;
            default:
                result += character;
                break;
            }
        }
        return result;
    }

    std::wstring QuoteArgument(std::wstring_view argument)
    {
        if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring_view::npos)
            return std::wstring(argument);
        std::wstring result = L"\"";
        size_t slashes = 0;
        for (const auto character : argument)
        {
            if (character == L'\\')
            {
                ++slashes;
                continue;
            }
            result.append(character == L'"' ? slashes * 2 + 1 : slashes, L'\\');
            slashes = 0;
            result += character;
        }
        result.append(slashes * 2, L'\\');
        return result + L'"';
    }

    int RunProcess(const Path& executable, const std::vector<std::wstring>& arguments, const Path& cwd, const Path& log,
                   unsigned timeoutMs)
    {
        std::wstring command = QuoteArgument(executable.wstring());
        for (const auto& argument : arguments)
            command += L" " + QuoteArgument(argument);
        if (!log.parent_path().empty())
            std::filesystem::create_directories(log.parent_path());
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        HANDLE output = CreateFileW(log.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (output == INVALID_HANDLE_VALUE)
            throw std::runtime_error("Cannot open build log.");
        HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
        if (input == INVALID_HANDLE_VALUE)
        {
            CloseHandle(output);
            throw std::runtime_error("Cannot open subprocess input.");
        }
        HANDLE job = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        {
            if (job)
                CloseHandle(job);
            if (input != INVALID_HANDLE_VALUE)
                CloseHandle(input);
            CloseHandle(output);
            throw std::runtime_error("Cannot create child build job.");
        }
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        startup.hStdOutput = output;
        startup.hStdError = output;
        startup.hStdInput = input;
        PROCESS_INFORMATION process{};
        const bool created = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
                                            CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                                            cwd.empty() ? nullptr : cwd.c_str(), &startup, &process) != FALSE;
        CloseHandle(output);
        if (input != INVALID_HANDLE_VALUE)
            CloseHandle(input);
        if (!created)
        {
            CloseHandle(job);
            throw std::runtime_error("Cannot start process: " + Utf8(executable));
        }
        if (!AssignProcessToJobObject(job, process.hProcess) || ResumeThread(process.hThread) == static_cast<DWORD>(-1))
        {
            TerminateProcess(process.hProcess, ERROR_CANCELLED);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            CloseHandle(job);
            throw std::runtime_error("Cannot manage build subprocess.");
        }
        CloseHandle(process.hThread);
        const auto wait = WaitForSingleObject(process.hProcess, timeoutMs);
        DWORD code = 1;
        const bool finished = wait == WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess, &code);
        if (!finished)
        {
            TerminateJobObject(job, ERROR_TIMEOUT);
            WaitForSingleObject(process.hProcess, 5000);
        }
        CloseHandle(process.hProcess);
        CloseHandle(job);
        if (!finished)
            throw std::runtime_error("Process timed out or could not be monitored: " + Utf8(executable));
        return static_cast<int>(code);
    }

    Path CurrentExecutable()
    {
        std::array<wchar_t, 32768> buffer{};
        const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (!length || length >= buffer.size())
            throw std::runtime_error("Cannot locate the project tool executable.");
        return Path(buffer.data());
    }

    Path FindMsbuild()
    {
        std::array<wchar_t, 32768> folder{};
        const auto length =
            GetEnvironmentVariableW(L"ProgramFiles(x86)", folder.data(), static_cast<DWORD>(folder.size()));
        if (!length || length >= folder.size())
            throw std::runtime_error("Cannot locate Visual Studio Installer.");
        const Path vswhere = Path(folder.data()) / "Microsoft Visual Studio/Installer/vswhere.exe";
        const auto temporary = std::filesystem::temp_directory_path() / ("wp1-msbuild-" + NewId() + ".txt");
        try
        {
            if (RunProcess(vswhere,
                           {L"-latest", L"-products", L"*", L"-requires", L"Microsoft.Component.MSBuild", L"-property",
                            L"installationPath", L"-utf8"},
                           {}, temporary) != 0)
                throw std::runtime_error("Cannot find Visual Studio with MSBuild.");
            std::ifstream input(temporary, std::ios::binary);
            std::string installation((std::istreambuf_iterator<char>(input)), {});
            input.close();
            while (!installation.empty() && (installation.back() == '\r' || installation.back() == '\n'))
                installation.pop_back();
            const auto msbuild = FromUtf8(installation) / "MSBuild/Current/Bin/MSBuild.exe";
            if (installation.empty() || !std::filesystem::is_regular_file(msbuild))
                throw std::runtime_error("Visual Studio with MSBuild is required.");
            std::filesystem::remove(temporary);
            return msbuild;
        }
        catch (...)
        {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            throw;
        }
    }

    void Progress(const Options& options, bool scripts, int step, std::string_view phase)
    {
        const auto folder = options.work / (scripts ? "generated/script-builds" : "generated/builds");
        WriteJsonAtomic(folder / (options.runId + ".progress.json"), {{"step", step}, {"phase", phase}});
    }
}
