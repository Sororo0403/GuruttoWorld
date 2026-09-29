#include <Engine/Core/CrashHandler.h>
#include <Engine/Core/DiagnosticPaths.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <DbgHelp.h>

#include <cstdio>
#include <exception>
#include <format>
#include <string>

#pragma comment(lib, "Dbghelp.lib")

namespace
{
    constexpr DWORD TerminateCode = 0xE0000001;
    constexpr DWORD CrashTimeout = 15000;

    struct CrashState
    {
        bool initialized = false;
        volatile LONG handling = 0;
        volatile LONG stopping = 0;
        HANDLE request = nullptr;
        HANDLE complete = nullptr;
        HANDLE worker = nullptr;
        DWORD threadId = 0;
        EXCEPTION_POINTERS* exception = nullptr;
        std::wstring dumpPath;
        std::wstring logPath;
        LPTOP_LEVEL_EXCEPTION_FILTER previousFilter = nullptr;
        std::terminate_handler previousTerminate = nullptr;
    };

    CrashState state;

    void WriteReport() noexcept
    {
        const auto* record = state.exception->ExceptionRecord;
        SYSTEMTIME time{};
        GetLocalTime(&time);
        char message[512]{};
        const int length = sprintf_s(message,
            "[%04u-%02u-%02u %02u:%02u:%02u] [CRASH] "
            "code=0x%08lX address=%p thread=%lu\r\n",
            static_cast<unsigned>(time.wYear), static_cast<unsigned>(time.wMonth),
            static_cast<unsigned>(time.wDay), static_cast<unsigned>(time.wHour),
            static_cast<unsigned>(time.wMinute), static_cast<unsigned>(time.wSecond),
            record->ExceptionCode, record->ExceptionAddress, state.threadId);
        OutputDebugStringA(message);

        // ダンプ生成が停止しても例外情報が残るよう、先に書き込んでフラッシュします。
        const HANDLE log = CreateFileW(state.logPath.c_str(), GENERIC_WRITE,
            FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (log != INVALID_HANDLE_VALUE)
        {
            DWORD written = 0;
            if (length > 0 && (!WriteFile(log, message, static_cast<DWORD>(length), &written, nullptr) ||
                written != static_cast<DWORD>(length)))
            {
                OutputDebugStringW(L"[CrashHandler] Cannot write the crash information.\n");
            }
            FlushFileBuffers(log);
        }
        else
        {
            OutputDebugStringW(L"[CrashHandler] Cannot open the crash log.\n");
        }

        const HANDLE dump = CreateFileW(state.dumpPath.c_str(), GENERIC_WRITE,
            FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        DWORD dumpError = ERROR_SUCCESS;
        if (dump != INVALID_HANDLE_VALUE)
        {
            MINIDUMP_EXCEPTION_INFORMATION information{};
            information.ThreadId = state.threadId;
            information.ExceptionPointers = state.exception;
            information.ClientPointers = FALSE;
            if (!MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump,
                MiniDumpNormal, &information, nullptr, nullptr))
            {
                dumpError = GetLastError();
            }
            FlushFileBuffers(dump);
            CloseHandle(dump);
        }
        else
        {
            dumpError = GetLastError();
        }

        const int resultLength = sprintf_s(message, "[CRASH] dump_error=0x%08lX\r\n", dumpError);
        OutputDebugStringA(message);
        OutputDebugStringW(state.dumpPath.c_str());
        OutputDebugStringW(L"\n");
        if (log != INVALID_HANDLE_VALUE)
        {
            DWORD written = 0;
            if (resultLength > 0 && (!WriteFile(log, message, static_cast<DWORD>(resultLength), &written, nullptr) ||
                written != static_cast<DWORD>(resultLength)))
            {
                OutputDebugStringW(L"[CrashHandler] Cannot append the dump result.\n");
            }
            FlushFileBuffers(log);
            CloseHandle(log);
        }
    }

    DWORD WINAPI Worker(void*) noexcept
    {
        if (WaitForSingleObject(state.request, INFINITE) == WAIT_OBJECT_0 &&
            InterlockedCompareExchange(&state.stopping, 0, 0) == 0)
        {
            WriteReport();
            SetEvent(state.complete);
        }
        return 0;
    }

    void RecordCrash(EXCEPTION_POINTERS* exception) noexcept
    {
        if (InterlockedCompareExchange(&state.handling, 1, 0) == 0)
        {
            state.exception = exception;
            state.threadId = GetCurrentThreadId();
            SetEvent(state.request);
        }
        if (WaitForSingleObject(state.complete, CrashTimeout) != WAIT_OBJECT_0)
        {
            OutputDebugStringW(L"[CrashHandler] Crash report timed out or failed.\n");
        }
    }

    LONG WINAPI UnhandledException(EXCEPTION_POINTERS* exception) noexcept
    {
        RecordCrash(exception);
        return EXCEPTION_EXECUTE_HANDLER;
    }

    void OnTerminate() noexcept
    {
        CONTEXT context{};
        RtlCaptureContext(&context);
        EXCEPTION_RECORD record{};
        record.ExceptionCode = TerminateCode;
        record.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
        record.ExceptionAddress = reinterpret_cast<void*>(context.Rip);
        EXCEPTION_POINTERS exception{ &record, &context };
        RecordCrash(&exception);
        TerminateProcess(GetCurrentProcess(), TerminateCode);
    }

    void CloseResources() noexcept
    {
        if (state.worker != nullptr)
        {
            InterlockedExchange(&state.stopping, 1);
            SetEvent(state.request);
            WaitForSingleObject(state.worker, INFINITE);
            CloseHandle(state.worker);
            state.worker = nullptr;
        }
        if (state.request != nullptr)
        {
            CloseHandle(state.request);
            state.request = nullptr;
        }
        if (state.complete != nullptr)
        {
            CloseHandle(state.complete);
            state.complete = nullptr;
        }
    }
}

namespace Engine
{
    bool CrashHandler::Initialize(const std::filesystem::path& directory)
    {
        if (state.initialized)
        {
            return true;
        }

        auto path = directory;
        if (path.empty())
        {
            const auto root = GetDiagnosticsRoot();
            if (root.empty())
            {
                return false;
            }
            path = root / "crashes";
        }
        std::error_code error;
        path = std::filesystem::absolute(path, error);
        if (error)
        {
            return false;
        }
        std::filesystem::create_directories(path, error);
        if (error)
        {
            return false;
        }

        SYSTEMTIME time{};
        GetLocalTime(&time);
        const auto name = std::format(L"Crash_{:04}{:02}{:02}_{:02}{:02}{:02}_{:03}_{}_{}",
            time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
            time.wMilliseconds, GetCurrentProcessId(), GetTickCount64());
        state.dumpPath = (path / (name + L".dmp")).wstring();
        state.logPath = (path / (name + L".log")).wstring();
        InterlockedExchange(&state.handling, 0);
        InterlockedExchange(&state.stopping, 0);
        state.request = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        state.complete = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (state.request == nullptr || state.complete == nullptr)
        {
            CloseResources();
            return false;
        }
        state.worker = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
        if (state.worker == nullptr)
        {
            CloseResources();
            return false;
        }
        state.previousFilter = SetUnhandledExceptionFilter(UnhandledException);
        state.previousTerminate = std::set_terminate(OnTerminate);
        state.initialized = true;
        return true;
    }

    void CrashHandler::Shutdown()
    {
        if (!state.initialized)
        {
            return;
        }
        SetUnhandledExceptionFilter(state.previousFilter);
        std::set_terminate(state.previousTerminate);
        CloseResources();
        state.initialized = false;
    }
}
