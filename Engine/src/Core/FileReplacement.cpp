#include <Engine/Core/FileReplacement.h>

namespace Engine
{
    FileReplacement::FileReplacement(const std::filesystem::path& path, const std::optional<std::string>& before,
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

    FileReplacement::~FileReplacement()
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

    auto FileReplacement::Apply() -> void
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

    auto FileReplacement::Restore() -> void
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

    auto FileReplacement::Save(const std::filesystem::path& path, const std::function<std::string()>& prepare,
                               bool overwrite) -> void
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

    auto FileReplacement::Write(const std::filesystem::path& path, const std::optional<std::string>& before,
                                std::string after) -> void
    {
        std::string error;
        if (!ApplyAll({{std::filesystem::absolute(path), before, std::move(after)}}, error,
                      []
                      {
                          return true;
                      }))
            throw std::runtime_error(error);
    }

    auto FileReplacement::ApplyAll(const std::vector<Change>& changes, std::string& error,
                                   const std::function<bool()>& commitDocument) -> bool
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

    auto FileReplacement::PrepareBatch(const std::vector<Change>& changes, SaveBatch& batch) -> void
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

    auto FileReplacement::FinishBatch(SaveBatch& batch, std::string& error) -> bool
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
                    error += " / 保存は完了しましたが、作業記録の掃除を再試行します：" + std::string(failure.what());
                    Log::Warning(error);
                }
                else
                    error += " / 復旧が必要です。次の保存前に再試行します：" + std::string(failure.what());
            }
        return batch.accepted;
    }

    auto FileReplacement::Unique(std::filesystem::path path, const wchar_t* kind) -> std::filesystem::path
    {
        static std::atomic<unsigned long long> sequence{0};
        path += L".wp1-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L"-" +
                std::to_wstring(sequence++) + L"-" + kind;
        return path;
    }

    auto FileReplacement::Open(const std::filesystem::path& path) -> HANDLE
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

    auto FileReplacement::Read(HANDLE file) -> std::string
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

    auto FileReplacement::Rename(HANDLE file, const std::filesystem::path& destination) -> void
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

    auto FileReplacement::Delete(HANDLE file) noexcept -> void
    {
        if (file == INVALID_HANDLE_VALUE)
            return;
        FILE_DISPOSITION_INFO disposition{TRUE};
        SetFileInformationByHandle(file, FileDispositionInfo, &disposition, sizeof(disposition));
    }

    auto FileReplacement::Close(HANDLE& file) noexcept -> void
    {
        if (file != INVALID_HANDLE_VALUE)
            CloseHandle(file);
        file = INVALID_HANDLE_VALUE;
    }

    [[noreturn]] auto FileReplacement::Fail(const char* operation, const std::filesystem::path& path) -> void
    {
        const auto error = GetLastError();
        throw std::runtime_error(std::string("Cannot ") + operation + " file (error " + std::to_string(error) +
                                 "): " + FileContents::Text(path));
    }
}
