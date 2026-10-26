#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <functional>
#include <vector>

namespace Editor
{
    struct FileChange
    {
        std::filesystem::path path;
        std::optional<std::string> before, after;
    };
    class FileTransaction final
    {
    public:
        /// <summary>絶対パスと現在の内容を、予定した書き込み内容に対応させます。</summary>
        static FileChange Prepare(const std::filesystem::path& path, std::optional<std::string> after);
        /// <summary>全対象の内容を照合し、ドキュメントの確定に失敗した場合は書き戻します。</summary>
        static bool Apply(const std::vector<FileChange>& changes, bool forward, std::string& error,
                          const std::function<bool()>& commitDocument);

    private:
        /// <summary>同じファイルを別表記で二重に操作する計画と、代替ストリームを拒否します。</summary>
        static void Validate(const std::vector<FileChange>& changes);
    };
}
