#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Engine
{
    class FileContents final
    {
    public:
        /// <summary>通常ファイルの内容を読み取ります。存在しない場合だけ未設定を返します。</summary>
        static std::optional<std::string> Read(const std::filesystem::path& path)
        {
            if (!std::filesystem::exists(path))
                return std::nullopt;
            if (!std::filesystem::is_regular_file(path))
                throw std::runtime_error("Not a regular file: " + Text(path));
            std::ifstream input(path, std::ios::binary);
            if (!input)
                throw std::runtime_error("Cannot open file: " + Text(path));
            std::string contents{std::istreambuf_iterator<char>(input), {}};
            if (input.bad())
                throw std::runtime_error("Cannot read file: " + Text(path));
            return contents;
        }
        /// <summary>内容を同期した一時ファイルから置き換えます。出力先の親フォルダーは作成しません。</summary>
        static void Write(const std::filesystem::path& path, std::string_view contents, bool overwrite = true)
        {
            static std::atomic<unsigned long long> sequence{0};
            auto temporary = path;
            temporary += L".wp1-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) +
                         L"-" + std::to_wstring(sequence++) + L".tmp";
            HANDLE file =
                CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE)
                Failure("create", path, GetLastError());
            try
            {
                if (GetFileType(file) != FILE_TYPE_DISK)
                    Failure("create regular", path, ERROR_INVALID_NAME);
                size_t offset = 0;
                while (offset < contents.size())
                {
                    const auto size = static_cast<DWORD>((std::min)(contents.size() - offset, size_t(1024 * 1024)));
                    DWORD written = 0;
                    if (!WriteFile(file, contents.data() + offset, size, &written, nullptr) || written == 0)
                        Failure("write", path, GetLastError());
                    offset += written;
                }
                if (!FlushFileBuffers(file))
                    Failure("flush", path, GetLastError());
                CloseHandle(file);
                file = INVALID_HANDLE_VALUE;
                if (!MoveFileExW(temporary.c_str(), path.c_str(),
                                 MOVEFILE_WRITE_THROUGH | (overwrite ? MOVEFILE_REPLACE_EXISTING : 0)))
                    Failure("replace", path, GetLastError());
            }
            catch (...)
            {
                if (file != INVALID_HANDLE_VALUE)
                    CloseHandle(file);
                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);
                throw;
            }
        }
        /// <summary>通常ファイルだけを削除します。フォルダーの削除は拒否します。</summary>
        static void Remove(const std::filesystem::path& path)
        {
            if (!std::filesystem::exists(path))
                return;
            if (!std::filesystem::is_regular_file(path))
                throw std::runtime_error("Not a regular file: " + Text(path));
            std::filesystem::remove(path);
        }
        /// <summary>ファイルパスを診断用のUTF-8へ変換します。</summary>
        static std::string Text(const std::filesystem::path& path)
        {
            const auto value = path.generic_u8string();
            return {value.begin(), value.end()};
        }

    private:
        /// <summary>OSの失敗コードと対象を保持して書き込みを中断します。</summary>
        [[noreturn]] static void Failure(const char* operation, const std::filesystem::path& path, DWORD code)
        {
            throw std::runtime_error(std::string("Cannot ") + operation + " file (error " + std::to_string(code) +
                                     "): " + Text(path));
        }
    };
}
