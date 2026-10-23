#pragma once
#include <Engine/Core/FileContents.h>
#include <Engine/Core/FileJournal.h>
#include <functional>
#include <memory>
#include <vector>

namespace Engine
{
    /// <summary>比較したファイルを排他的に保持し、上書きしない改名で候補を確定します。</summary>
    class FileReplacement final
    {
        struct SaveBatch
        {
            std::unique_ptr<FileSaveLock> lock;
            std::vector<std::unique_ptr<FileReplacement>> replacements;
            std::unique_ptr<FileJournal> journal;
            bool accepted = false;
        };

    public:
        struct Change
        {
            std::filesystem::path path;
            std::optional<std::string> before, after;
        };
        /// <summary>同期済みの候補を用意してから保存元を保持し、外部変更を照合します。</summary>
        FileReplacement(const std::filesystem::path& path, const std::optional<std::string>& before,
                        const std::optional<std::string>& after)
            : path_(path), stagedPath_(Unique(path, L"stage")), backupPath_(Unique(path, L"backup"))
        {
            bool stagedCreated = false;
            try
            {
                if (after)
                {
                    std::filesystem::create_directories(path.parent_path());
                    FileContents::Write(stagedPath_, *after, false);
                    stagedCreated = true;
                    staged_ = Open(stagedPath_);
                }
                if (before)
                {
                    original_ = Open(path_);
                    if (Read(original_) != *before)
                        throw std::runtime_error("ファイルは外部で変更されています：" + FileContents::Text(path_));
                }
                else if (std::filesystem::exists(path_))
                    throw std::runtime_error("保存先が外部で作成されています：" + FileContents::Text(path_));
            }
            catch (...)
            {
                Close(staged_);
                Close(original_);
                std::error_code ignored;
                if (stagedCreated)
                    std::filesystem::remove(stagedPath_, ignored);
                throw;
            }
        }
        /// <summary>保持中のファイルは複製しません。</summary>
        FileReplacement(const FileReplacement&) = delete;
        /// <summary>保持中のファイルは代入しません。</summary>
        FileReplacement& operator=(const FileReplacement&) = delete;
        /// <summary>未確定なら復元し、復元競合時の旧ファイルはバックアップ名で残します。</summary>
        ~FileReplacement()
        {
            if (!committed_)
            {
                try
                {
                    Restore();
                }
                catch (...)
                {
                }
            }
            if (committed_ && originalMoved_)
                Delete(original_);
            if (!installed_)
                Delete(staged_);
            Close(staged_);
            Close(original_);
        }
        /// <summary>保持した旧ファイルを退避し、空いた名前へ非上書きで候補を設置します。</summary>
        void Apply()
        {
            if (original_ != INVALID_HANDLE_VALUE)
            {
                Rename(original_, backupPath_);
                originalMoved_ = true;
            }
            if (staged_ != INVALID_HANDLE_VALUE)
            {
                Rename(staged_, path_);
                installed_ = true;
            }
        }
        /// <summary>所有する候補だけを退かし、外部で作成されたファイルを上書きせず復元します。</summary>
        void Restore()
        {
            if (installed_)
            {
                Rename(staged_, stagedPath_);
                installed_ = false;
            }
            if (originalMoved_)
            {
                try
                {
                    Rename(original_, path_);
                }
                catch (const std::exception& failure)
                {
                    throw std::runtime_error(std::string(failure.what()) + " / 旧内容の保管先：" +
                                             FileContents::Text(backupPath_));
                }
                originalMoved_ = false;
            }
        }
        /// <summary>全対象の保存成功後に候補を保持し、旧内容を解放可能にします。</summary>
        void Commit() noexcept
        {
            committed_ = true;
        }
        /// <summary>保存領域を復旧してから内容を構築し、保存直前の外部変更も照合します。</summary>
        static void Save(const std::filesystem::path& path, const std::function<std::string()>& prepare,
                         bool overwrite = true)
        {
            const auto absolute = std::filesystem::absolute(path);
            const auto root = FileSaveLock::Root(absolute);
            FileSaveLock lock(root);
            if (lock.Outermost())
                FileJournal::RecoverLocked(root);
            const auto before = FileContents::Read(absolute);
            if (before && !overwrite)
                throw std::runtime_error("保存先は既に存在します：" + FileContents::Text(absolute));
            Write(absolute, before, prepare());
        }
        /// <summary>単一ファイルにも比較・排他・復元を共通適用します。</summary>
        static void Write(const std::filesystem::path& path, const std::optional<std::string>& before,
                          std::string after)
        {
            std::string error;
            if (!ApplyAll({{std::filesystem::absolute(path), before, std::move(after)}}, error,
                          []
                          {
                              return true;
                          }))
                throw std::runtime_error(error);
        }
        /// <summary>全対象の復旧記録を公開してから変更し、失敗時は外部内容も保管して旧版へ戻します。</summary>
        static bool ApplyAll(const std::vector<Change>& changes, std::string& error,
                             const std::function<bool()>& commitDocument)
        {
            error.clear();
            SaveBatch batch;
            try
            {
                if (changes.empty())
                    return commitDocument();
                PrepareBatch(changes, batch);
                // 受理後のメモリー更新と同期失敗が食い違わないよう、確定方針を先に同期します。
                if (batch.journal)
                {
                    batch.journal->CommitIntent();
                    batch.journal->Complete();
                }
                batch.accepted = commitDocument();
                if (!batch.accepted)
                    throw std::runtime_error("ドキュメントを適用できません。");
                for (auto& replacement : batch.replacements)
                    replacement->Commit();
                if (batch.journal)
                    try
                    {
                        batch.journal->Accept();
                    }
                    catch (const std::exception& failure)
                    {
                        error = "保存は完了しましたが、受理結果を記録できないため両世代を保管します：" +
                                std::string(failure.what());
                        Log::Warning(error);
                    }
            }
            catch (const std::exception& failure)
            {
                error = failure.what();
            }
            catch (...)
            {
                error = "保存中に不明な例外が発生しました。";
            }
            return FinishBatch(batch, error);
        }

    private:
        /// <summary>排他を取得し、全作業ファイルと記録を準備してから置換します。</summary>
        static void PrepareBatch(const std::vector<Change>& changes, SaveBatch& batch)
        {
            std::vector<FileJournal::Entry> entries;
            entries.reserve(changes.size());
            std::transform(changes.begin(), changes.end(), std::back_inserter(entries),
                           [](const auto& change)
                           {
                               return FileJournal::Entry{change.path, {}, {}, change.before, change.after};
                           });
            const auto root = FileJournal::Root(entries);
            batch.lock = std::make_unique<FileSaveLock>(root);
            if (batch.lock->Outermost())
                FileJournal::RecoverLocked(root);
            for (size_t i = 0; i < changes.size(); ++i)
            {
                const auto& change = changes[i];
                auto replacement = std::make_unique<FileReplacement>(change.path, change.before, change.after);
                entries[i].stage = replacement->stagedPath_;
                entries[i].backup = replacement->backupPath_;
                if (replacement->original_ != INVALID_HANDLE_VALUE)
                    entries[i].beforeId = FileJournal::Identity(replacement->original_);
                if (replacement->staged_ != INVALID_HANDLE_VALUE)
                    entries[i].afterId = FileJournal::Identity(replacement->staged_);
                batch.replacements.push_back(std::move(replacement));
            }
            std::vector<FileJournal::Entry> modified;
            for (auto& entry : entries)
                if (entry.before != entry.after)
                    modified.push_back(std::move(entry));
            if (!modified.empty())
                batch.journal = std::make_unique<FileJournal>(root, modified);
            for (size_t i = 0; i < changes.size(); ++i)
                if (changes[i].before != changes[i].after)
                    batch.replacements[i]->Apply();
        }
        /// <summary>失敗時の取消方針を同期し、対象の保持を解放してから記録を掃除します。</summary>
        static bool FinishBatch(SaveBatch& batch, std::string& error)
        {
            if (!batch.accepted && batch.journal)
            {
                try
                {
                    batch.journal->RollbackIntent();
                }
                catch (const std::exception& failure)
                {
                    // 取消方針を記録できない場合は、確定済み世代を壊さず次回の復旧へ渡します。
                    for (auto& replacement : batch.replacements)
                        replacement->Commit();
                    error += " / " + std::string(failure.what());
                    return false;
                }
            }
            batch.replacements.clear();
            if (batch.journal)
                try
                {
                    const auto conflicts = batch.journal->Finish();
                    if (!conflicts.empty())
                        error +=
                            " / 保存内容を保管して復旧しました：" + FileContents::Text(conflicts.front().parent_path());
                }
                catch (const std::exception& failure)
                {
                    if (batch.accepted)
                    {
                        error +=
                            " / 保存は完了しましたが、作業記録の掃除を再試行します：" + std::string(failure.what());
                        Log::Warning(error);
                    }
                    else
                        error += " / 復旧が必要です。次の保存前に再試行します：" + std::string(failure.what());
                }
            return batch.accepted;
        }
        /// <summary>同じフォルダーに衝突しない作業名を生成します。</summary>
        static std::filesystem::path Unique(std::filesystem::path path, const wchar_t* kind)
        {
            static std::atomic<unsigned long long> sequence{0};
            path += L".wp1-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) +
                    L"-" + std::to_wstring(sequence++) + L"-" + kind;
            return path;
        }
        /// <summary>他の書込・削除・置換を拒否し、改名にも同じハンドルを使用します。</summary>
        static HANDLE Open(const std::filesystem::path& path)
        {
            auto file = CreateFileW(path.c_str(), GENERIC_READ | DELETE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                    FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (file == INVALID_HANDLE_VALUE)
                Fail("lock", path);
            BY_HANDLE_FILE_INFORMATION information{};
            if (!GetFileInformationByHandle(file, &information) ||
                (information.dwFileAttributes &
                 (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_READONLY)))
            {
                CloseHandle(file);
                throw std::runtime_error("通常の書込可能なファイルを保持できません：" + FileContents::Text(path));
            }
            return file;
        }
        /// <summary>保持した同じファイルの内容を比較用に読み取ります。</summary>
        static std::string Read(HANDLE file)
        {
            std::string contents;
            char buffer[65536];
            DWORD count = 0;
            while (true)
            {
                if (!ReadFile(file, buffer, sizeof(buffer), &count, nullptr))
                    throw std::runtime_error("保持したファイルの読込に失敗しました");
                if (!count)
                    return contents;
                contents.append(buffer, count);
            }
        }
        /// <summary>他のファイルを上書きせず、保持中の実体だけを改名します。</summary>
        static void Rename(HANDLE file, const std::filesystem::path& destination)
        {
            const auto name = std::filesystem::absolute(destination).native();
            const auto bytes = static_cast<DWORD>(name.size() * sizeof(wchar_t));
            std::vector<unsigned char> storage(sizeof(FILE_RENAME_INFO) + bytes);
            auto* information = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
            information->ReplaceIfExists = FALSE;
            information->RootDirectory = nullptr;
            information->FileNameLength = bytes;
            std::copy(name.begin(), name.end(), information->FileName);
            if (!SetFileInformationByHandle(file, FileRenameInfo, information, static_cast<DWORD>(storage.size())))
                Fail("rename", destination);
        }
        /// <summary>自分が保持する不要な作業ファイルだけを削除対象にします。</summary>
        static void Delete(HANDLE file) noexcept
        {
            if (file == INVALID_HANDLE_VALUE)
                return;
            FILE_DISPOSITION_INFO disposition{TRUE};
            SetFileInformationByHandle(file, FileDispositionInfo, &disposition, sizeof(disposition));
        }
        /// <summary>保持を解放します。</summary>
        static void Close(HANDLE& file) noexcept
        {
            if (file != INVALID_HANDLE_VALUE)
                CloseHandle(file);
            file = INVALID_HANDLE_VALUE;
        }
        /// <summary>対象とOSの失敗理由を保持します。</summary>
        [[noreturn]] static void Fail(const char* operation, const std::filesystem::path& path)
        {
            const auto error = GetLastError();
            throw std::runtime_error(std::string("Cannot ") + operation + " file (error " + std::to_string(error) +
                                     "): " + FileContents::Text(path));
        }
        std::filesystem::path path_, stagedPath_, backupPath_;
        HANDLE original_ = INVALID_HANDLE_VALUE, staged_ = INVALID_HANDLE_VALUE;
        bool originalMoved_ = false, installed_ = false, committed_ = false;
    };
}
