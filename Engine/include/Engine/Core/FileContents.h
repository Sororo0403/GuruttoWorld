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
        static std::optional<std::string> Read(const std::filesystem::path& path);
        /// <summary>内容を同期した一時ファイルから置き換えます。出力先の親フォルダーは作成しません。</summary>
        static void Write(const std::filesystem::path& path, std::string_view contents, bool overwrite = true);
        /// <summary>通常ファイルだけを削除します。フォルダーの削除は拒否します。</summary>
        static void Remove(const std::filesystem::path& path);
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
