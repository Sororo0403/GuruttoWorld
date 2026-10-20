#pragma once
#include <Engine/Core/FileContents.h>
#include <iostream>

namespace FileSaveRecoveryValidation
{
    /// <summary>別の翻訳単位で故障注入する子プロセスを、通常回帰から実行します。</summary>
    inline void Run()
    {
        std::wstring executable(32768, L'\0');
        const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (!length || length >= executable.size())
            throw std::runtime_error("保存復旧テストの配置を取得できません");
        executable.resize(length);
        const auto path = std::filesystem::path(executable).parent_path().parent_path() /
                          "FileSaveRecoveryValidation/FileSaveRecoveryValidation.exe";
        auto command = L"\"" + path.native() + L"\"";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                            &startup, &process))
            throw std::runtime_error("保存復旧テストを起動できません");
        const auto wait = WaitForSingleObject(process.hProcess, 60000);
        DWORD code = 1;
        if (wait != WAIT_OBJECT_0)
        {
            TerminateProcess(process.hProcess, 99);
            WaitForSingleObject(process.hProcess, 10000);
        }
        GetExitCodeProcess(process.hProcess, &code);
        CloseHandle(process.hProcess);
        CloseHandle(process.hThread);
        if (wait != WAIT_OBJECT_0 || code != 0)
            throw std::runtime_error("保存復旧・同時編集の回帰テストが失敗しました");
        std::cout << "PASS: crash recovery, conflict preservation and concurrent metadata processes\n";
    }
}
