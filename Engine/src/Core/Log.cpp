#include <Engine/Core/Log.h>
#include <Engine/Core/DiagnosticPaths.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <fstream>
#include <format>
#include <limits>
#include <mutex>
#include <string>

namespace
{
    struct LogState
    {
        std::mutex mutex;
        std::ofstream file;
    };

    LogState& GetState()
    {
        static LogState state;
        return state;
    }

    void WriteDebugger(const std::string& text)
    {
        if (text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
        {
            OutputDebugStringW(L"[Log] Message is too large.\n");
            return;
        }

        const int size = static_cast<int>(text.size());
        const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), size, nullptr, 0);
        if (length == 0)
        {
            return;
        }

        std::wstring wide(static_cast<std::size_t>(length), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), size, wide.data(), length);
        OutputDebugStringW(wide.c_str());
    }

    const char* LevelName(Engine::LogLevel level)
    {
        switch (level)
        {
        case Engine::LogLevel::Debug: return "DEBUG";
        case Engine::LogLevel::Info: return "INFO";
        case Engine::LogLevel::Warning: return "WARNING";
        case Engine::LogLevel::Error: return "ERROR";
        default: return "UNKNOWN";
        }
    }
}

namespace Engine
{
    bool Log::Initialize(const std::filesystem::path& filePath)
    {
        auto& state = GetState();
        const std::lock_guard lock(state.mutex);
        auto path = filePath;
        if (path.empty())
        {
            const auto directory = GetDiagnosticsRoot();
            if (directory.empty())
            {
                OutputDebugStringW(L"[Log] Cannot resolve LocalAppData.\n");
                return false;
            }
            path = directory / "logs" / "App.log";
        }

        std::error_code error;
        if (path.has_parent_path())
        {
            std::filesystem::create_directories(path.parent_path(), error);
        }
        if (error)
        {
            OutputDebugStringW(L"[Log] Cannot create the log directory.\n");
            return false;
        }

        std::ofstream file(path, std::ios::app | std::ios::binary);
        if (!file)
        {
            OutputDebugStringW(L"[Log] Cannot open the log file.\n");
            return false;
        }
        state.file = std::move(file);
        return true;
    }

    void Log::Shutdown()
    {
        auto& state = GetState();
        const std::lock_guard lock(state.mutex);
        if (state.file.is_open())
        {
            state.file.close();
        }
    }

    void Log::Write(LogLevel level, std::string_view message)
    {
        auto& state = GetState();
        const std::lock_guard lock(state.mutex);
        SYSTEMTIME time{};
        GetLocalTime(&time);
        const auto entry = std::format(
            "[{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}] [{}] {}\n",
            time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
            time.wSecond, time.wMilliseconds, LevelName(level), message);

        WriteDebugger(entry);
        if (state.file.is_open())
        {
            state.file << entry;
            state.file.flush();
            if (!state.file)
            {
                OutputDebugStringW(L"[Log] Failed to write the log file.\n");
                state.file.close();
            }
        }
    }

    void Log::Debug(std::string_view message) { Write(LogLevel::Debug, message); }
    void Log::Info(std::string_view message) { Write(LogLevel::Info, message); }
    void Log::Warning(std::string_view message) { Write(LogLevel::Warning, message); }
    void Log::Error(std::string_view message) { Write(LogLevel::Error, message); }
}
