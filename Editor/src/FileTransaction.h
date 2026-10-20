#pragma once
#include <Engine/Core/FileContents.h>
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
        static FileChange Prepare(const std::filesystem::path& path, std::optional<std::string> after)
        {
            const auto absolute = std::filesystem::weakly_canonical(std::filesystem::absolute(path));
            return {absolute, Engine::FileContents::Read(absolute), std::move(after)};
        }
        /// <summary>全対象の内容を照合し、ドキュメントの確定に失敗した場合は書き戻します。</summary>
        static bool Apply(const std::vector<FileChange>& changes, bool forward, std::string& error,
                          const std::function<bool()>& commitDocument)
        {
            error.clear();
            std::vector<size_t> written;
            written.reserve(changes.size());
            try
            {
                Validate(changes);
                for (const auto& change : changes)
                    Check(change, forward);
                for (size_t i = 0; i < changes.size(); ++i)
                {
                    const auto& change = changes[i];
                    if (change.before == change.after)
                        continue;
                    Check(change, forward);
                    Replace(change.path, forward ? change.after : change.before,
                            (forward ? change.before : change.after).has_value());
                    written.push_back(i);
                }
                if (commitDocument())
                    return true;
                if (error.empty())
                    error = "ドキュメントを適用できません。";
            }
            catch (const std::exception& failure)
            {
                error = failure.what();
            }
            for (auto entry = written.rbegin(); entry != written.rend(); ++entry)
            {
                const auto& change = changes[*entry];
                try
                {
                    // 復元中も外部変更を上書きせず、競合した対象を診断します。
                    Check(change, !forward);
                    Replace(change.path, forward ? change.before : change.after,
                            (forward ? change.after : change.before).has_value());
                }
                catch (const std::exception& failure)
                {
                    error += " / 復元に失敗しました：" + std::string(failure.what());
                }
            }
            return false;
        }

    private:
        /// <summary>同じファイルを別表記で二重に操作する計画と、代替ストリームを拒否します。</summary>
        static void Validate(const std::vector<FileChange>& changes)
        {
            std::vector<std::wstring> targets;
            for (const auto& change : changes)
            {
                if (!change.path.is_absolute())
                    throw std::runtime_error("File transaction requires absolute paths");
                for (const auto& part : change.path.relative_path())
                    if (part.native().find(L':') != std::wstring::npos)
                        throw std::runtime_error("File transaction cannot target an alternate stream");
                const auto target = std::filesystem::weakly_canonical(change.path).native();
                if (CompareStringOrdinal(change.path.lexically_normal().c_str(), -1, target.c_str(), -1, TRUE) !=
                    CSTR_EQUAL)
                    throw std::runtime_error("File transaction path was redirected");
                for (const auto& previous : targets)
                    if (CompareStringOrdinal(previous.c_str(), -1, target.c_str(), -1, TRUE) == CSTR_EQUAL)
                        throw std::runtime_error("Duplicate file transaction target");
                targets.push_back(target);
            }
        }
        /// <summary>予定した変更前の内容と一致することを確認します。</summary>
        static void Check(const FileChange& change, bool forward)
        {
            const auto canonical = std::filesystem::weakly_canonical(change.path);
            if (CompareStringOrdinal(change.path.lexically_normal().c_str(), -1, canonical.c_str(), -1, TRUE) !=
                CSTR_EQUAL)
                throw std::runtime_error("ファイルの参照先が変更されています：" +
                                         Engine::FileContents::Text(change.path));
            if (Engine::FileContents::Read(change.path) != (forward ? change.before : change.after))
                throw std::runtime_error("ファイルは外部で変更されています：" +
                                         Engine::FileContents::Text(change.path));
        }
        /// <summary>ファイルの作成・置換・削除を共通の書き込み処理へ渡します。</summary>
        static void Replace(const std::filesystem::path& path, const std::optional<std::string>& contents,
                            bool overwrite)
        {
            if (contents)
            {
                std::filesystem::create_directories(path.parent_path());
                Engine::FileContents::Write(path, *contents, overwrite);
            }
            else
                Engine::FileContents::Remove(path);
        }
    };
}
