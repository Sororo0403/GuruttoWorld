#pragma once
#include <Engine/Core/FileSaveLock.h>
#include <Engine/Core/Json.h>
#include <Engine/Core/Log.h>
#include <memory>

namespace Engine
{
    /// <summary>保存前後の全内容と確定境界を同期し、異常終了後に同じ世代へ回復します。</summary>
    class FileJournal final
    {
    public:
        struct Entry
        {
            std::filesystem::path path, stage, backup;
            std::optional<std::string> before, after;
            std::string beforeId, afterId;
        };
        /// <summary>内容が同じ別ファイルを誤って所有物と判定しないよう、実体IDを保持します。</summary>
        static std::string Identity(HANDLE handle)
        {
            FILE_ID_INFO info{};
            if (!GetFileInformationByHandleEx(handle, FileIdInfo, &info, sizeof(info)))
                throw std::runtime_error("保存対象の実体IDを取得できません");
            std::string identity = std::to_string(info.VolumeSerialNumber) + ":";
            constexpr char Hex[] = "0123456789abcdef";
            for (const auto byte : info.FileId.Identifier)
            {
                identity += Hex[byte >> 4];
                identity += Hex[byte & 15];
            }
            return identity;
        }
        /// <summary>変更先を含む保存領域を求めます。</summary>
        static std::filesystem::path Root(const std::vector<Entry>& entries)
        {
            if (entries.empty() || entries.size() > 1024)
                throw std::runtime_error("一括保存の対象数は1から1024件にしてください。");
            const auto root = FileSaveLock::Root(entries.front().path);
            for (const auto& entry : entries)
                if (CompareStringOrdinal(root.c_str(), -1, FileSaveLock::Root(entry.path).c_str(), -1, TRUE) !=
                    CSTR_EQUAL)
                    throw std::runtime_error("異なる保存領域の変更は、領域ごとに分けて保存してください。");
            return root;
        }
        /// <summary>元のパスを変更する前に、復旧に必要な全コピーと一覧を永続化します。</summary>
        FileJournal(const std::filesystem::path& root, const std::vector<Entry>& entries)
            : directory_(root / ".wp1-transactions" / (Token() + ".pending"))
        {
            std::filesystem::create_directories(directory_);
            auto lease = CreateFileW((directory_ / "lease").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                     CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (lease == INVALID_HANDLE_VALUE)
                throw std::runtime_error("保存記録を保持できません");
            lease_.reset(lease);
            Json records = Json::array();
            size_t index = 0;
            for (const auto& entry : entries)
            {
                const auto name = std::to_string(index++);
                if (entry.before)
                    FileContents::Write(directory_ / (name + ".before"), *entry.before, false);
                if (entry.after)
                    FileContents::Write(directory_ / (name + ".after"), *entry.after, false);
                records.push_back({{"path", FileContents::Text(entry.path)},
                                   {"stage", FileContents::Text(entry.stage)},
                                   {"backup", FileContents::Text(entry.backup)},
                                   {"beforeId", entry.beforeId},
                                   {"afterId", entry.afterId},
                                   {"before", entry.before.has_value()},
                                   {"after", entry.after.has_value()}});
            }
            FileContents::Write(directory_ / "manifest.json", Json{{"version", 4}, {"entries", records}}.dump(), false);
        }
        /// <summary>復旧記録の所有者は複製しません。</summary>
        FileJournal(const FileJournal&) = delete;
        /// <summary>復旧記録の所有者は代入しません。</summary>
        FileJournal& operator=(const FileJournal&) = delete;
        /// <summary>ドキュメントを受理する前に新版への回復方針を同期します。</summary>
        void CommitIntent()
        {
            FileContents::Write(directory_ / "committed", "committed\n", false);
        }
        /// <summary>全対象を保持中に、以後は対象へ再適用しない保存完了境界を同期します。</summary>
        void Complete()
        {
            FileContents::Write(directory_ / "completed", "completed\n", false);
        }
        /// <summary>ドキュメント拒否時は、ファイルを戻す前に旧版への回復方針を確定します。</summary>
        void RollbackIntent()
        {
            FileContents::Write(directory_ / "aborted", "aborted\n", false);
        }
        /// <summary>メモリーの受理後に掃除を許可し、記録できない場合は両世代のコピーを残します。</summary>
        void Accept()
        {
            FileContents::Write(directory_ / "accepted", "accepted\n", false);
        }
        /// <summary>保持ハンドルの解放後に、残った作業ファイルを回収します。</summary>
        std::vector<std::filesystem::path> Finish()
        {
            lease_.reset();
            return RecoverOne(directory_.parent_path().parent_path(), directory_);
        }
        /// <summary>対象の保存領域を開く前に、前回の未完了保存を回復します。</summary>
        static std::vector<std::filesystem::path> Recover(const std::filesystem::path& root)
        {
            FileSaveLock lock(root);
            if (!lock.Outermost())
                return {};
            return RecoverLocked(root);
        }
        /// <summary>保存領域の排他中に旧保存領域も毎回探索し、遅れて現れた記録も使用前に回復します。</summary>
        static std::vector<std::filesystem::path> RecoverLocked(const std::filesystem::path& root)
        {
            std::vector<std::filesystem::path> recovered, pending;
            for (const auto& saveRoot : SaveRoots(root))
            {
                const auto directory = saveRoot / ".wp1-transactions";
                if (!std::filesystem::is_directory(directory))
                    continue;
                if (!Inside(root, directory))
                    throw std::runtime_error("復旧記録が保存領域外です：" + FileContents::Text(directory));
                RejectReparse(directory);
                for (const auto& entry : std::filesystem::directory_iterator(directory))
                {
                    if (entry.is_directory() && entry.path().extension() == ".pending")
                        pending.push_back(entry.path());
                    else if (entry.is_directory() && entry.path().extension() == ".completed")
                    {
                        RejectReparse(entry.path());
                        std::filesystem::remove_all(entry.path());
                    }
                }
            }
            std::sort(pending.begin(), pending.end());
            for (const auto& path : pending)
            {
                const auto conflicts = RecoverOne(root, path);
                if (!conflicts.empty())
                    Log::Warning("保存を復旧し、外部内容を保管しました：" +
                                 FileContents::Text(conflicts.front().parent_path()));
                recovered.insert(recovered.end(), conflicts.begin(), conflicts.end());
            }
            return recovered;
        }

    private:
        /// <summary>旧実装の保存領域を再探索し、探索後に追加された記録を見落とさないようにします。</summary>
        static std::vector<std::filesystem::path> SaveRoots(const std::filesystem::path& root)
        {
            const auto canonical = std::filesystem::weakly_canonical(std::filesystem::absolute(root));
            std::vector<std::filesystem::path> roots{canonical};
            for (const auto* name : {"Assets", "Shaders"})
            {
                const auto folder = canonical / name;
                if (!std::filesystem::is_directory(folder))
                    continue;
                const auto folderAttributes = GetFileAttributesW(folder.c_str());
                if (folderAttributes == INVALID_FILE_ATTRIBUTES || (folderAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                    continue;
                for (auto it = std::filesystem::recursive_directory_iterator(folder);
                     it != std::filesystem::recursive_directory_iterator(); ++it)
                {
                    if (!it->is_directory())
                        continue;
                    const auto path = it->path();
                    const auto attributes = GetFileAttributesW(path.c_str());
                    if (attributes == INVALID_FILE_ATTRIBUTES)
                        throw std::runtime_error("旧保存領域を確認できません：" + FileContents::Text(path));
                    if (attributes & FILE_ATTRIBUTE_REPARSE_POINT)
                        it.disable_recursion_pending();
                    else if (FileSaveLock::IsJournalPath(path.filename()))
                    {
                        roots.push_back(path.parent_path());
                        it.disable_recursion_pending();
                    }
                    else if (std::filesystem::is_regular_file(path / "WP1.project.json") ||
                             std::filesystem::is_regular_file(path / "WP1.runtime.json"))
                        it.disable_recursion_pending();
                }
            }
            return roots;
        }
        /// <summary>復旧の完了名を非上書きで公開し、一時的な共有違反だけを短く再試行します。</summary>
        static void RenameDirectory(const std::filesystem::path& from, const std::filesystem::path& to)
        {
            for (unsigned int attempt = 0; attempt < 9; ++attempt)
            {
                if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH))
                    return;
                const auto error = GetLastError();
                if (attempt == 8 || (error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION))
                    throw std::runtime_error("復旧記録の完了名を公開できません（" + std::to_string(error) + "）：" +
                                             FileContents::Text(from));
                Sleep((std::min)(1u << attempt, 16u));
            }
        }
        /// <summary>同じプロセスでも保存ごとに異なる記録名を作ります。</summary>
        static std::string Token()
        {
            static std::atomic<unsigned long long> sequence{0};
            return std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()) + "-" +
                   std::to_string(sequence++);
        }
        /// <summary>UTF-8で保存したパスを復元します。</summary>
        static std::filesystem::path Path(const std::string& text)
        {
            return std::filesystem::path(std::u8string(text.begin(), text.end()));
        }
        /// <summary>保存領域の外や、再解析によって外へ移されたパスを拒否します。</summary>
        static bool Inside(const std::filesystem::path& root, const std::filesystem::path& path)
        {
            const auto base = std::filesystem::weakly_canonical(std::filesystem::absolute(root));
            const auto target = std::filesystem::weakly_canonical(std::filesystem::absolute(path));
            auto part = target.begin();
            for (const auto& expected : base)
            {
                if (part == target.end() ||
                    CompareStringOrdinal(expected.c_str(), -1, part->c_str(), -1, TRUE) != CSTR_EQUAL)
                    return false;
                ++part;
            }
            return part != target.end();
        }
        /// <summary>復旧用フォルダーとファイルを別の場所へ転送する入力は拒否します。</summary>
        static void RejectReparse(const std::filesystem::path& path)
        {
            const auto attributes = GetFileAttributesW(path.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
                throw std::runtime_error("復旧先を確認できません：" + FileContents::Text(path));
        }
        /// <summary>書込と改名を排他した、通常ファイルだけを保持します。</summary>
        static HANDLE Open(const std::filesystem::path& path)
        {
            auto handle = CreateFileW(path.c_str(), GENERIC_READ | DELETE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                      FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (handle == INVALID_HANDLE_VALUE)
                throw std::runtime_error("復旧するファイルを保持できません：" + FileContents::Text(path));
            BY_HANDLE_FILE_INFORMATION info{};
            if (!GetFileInformationByHandle(handle, &info) ||
                (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)))
            {
                CloseHandle(handle);
                throw std::runtime_error("復旧対象は通常ファイルではありません");
            }
            return handle;
        }
        /// <summary>保持した実体を比較し、同じ名前への差し替え競合を避けます。</summary>
        static std::string Read(HANDLE handle)
        {
            std::string text;
            char buffer[65536];
            DWORD count = 0;
            while (true)
            {
                if (!ReadFile(handle, buffer, sizeof(buffer), &count, nullptr))
                    throw std::runtime_error("復旧対象を読み取れません");
                if (!count)
                    return text;
                text.append(buffer, count);
            }
        }
        /// <summary>保持した実体だけを、他の内容を上書きせずに移動します。</summary>
        static void Move(HANDLE handle, const std::filesystem::path& to)
        {
            const auto name = std::filesystem::absolute(to).native();
            const auto bytes = static_cast<DWORD>(name.size() * sizeof(wchar_t));
            std::vector<unsigned char> storage(sizeof(FILE_RENAME_INFO) + bytes);
            auto* information = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
            information->ReplaceIfExists = FALSE;
            information->FileNameLength = bytes;
            std::copy(name.begin(), name.end(), information->FileName);
            if (!SetFileInformationByHandle(handle, FileRenameInfo, information, static_cast<DWORD>(storage.size())))
                throw std::runtime_error("復旧用の退避に失敗しました：" + FileContents::Text(to));
        }
        /// <summary>現在の内容を保存し、外部版の場合は掃除しない保管先に分類します。</summary>
        static bool Retire(const std::filesystem::path& path, const std::filesystem::path& directory,
                           const std::optional<std::string>& first, const std::optional<std::string>& second,
                           const std::string& firstId, const std::string& secondId, bool keepIfDesired = false)
        {
            if (!std::filesystem::exists(path))
                return false;
            auto handle = Open(path);
            try
            {
                const auto text = Read(handle);
                const bool retained = keepIfDesired && first && text == *first;
                if (!keepIfDesired || !first || text != *first)
                {
                    const auto identity = Identity(handle);
                    const bool external = !(first && text == *first && identity == firstId) &&
                                          !(second && text == *second && identity == secondId);
                    const auto token = Token();
                    const auto destination = directory / (token + (external ? ".conflict" : ".retired"));
                    if (external)
                        FileContents::Write(directory / (token + ".info.json"),
                                            Json{{"source", FileContents::Text(path)},
                                                 {"preserved", FileContents::Text(destination.filename())}}
                                                .dump(),
                                            false);
                    Move(handle, destination);
                }
                CloseHandle(handle);
                return retained;
            }
            catch (...)
            {
                CloseHandle(handle);
                throw;
            }
        }
        /// <summary>全コピーを先に検証してから、確定記録に従って回復を再開します。</summary>
        static std::vector<std::filesystem::path> RecoverOne(const std::filesystem::path& root,
                                                             const std::filesystem::path& directory)
        {
            RejectReparse(directory);
            const auto leasePath = directory / "lease";
            auto lease = CreateFileW(leasePath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                     FILE_ATTRIBUTE_NORMAL, nullptr);
            if (lease == INVALID_HANDLE_VALUE)
            {
                const auto error = GetLastError();
                if (error == ERROR_SHARING_VIOLATION &&
                    directory.filename().string().starts_with(std::to_string(GetCurrentProcessId()) + "-"))
                    return {};
                if (error != ERROR_FILE_NOT_FOUND)
                    throw std::runtime_error("未完了の保存記録を保持できません：" + FileContents::Text(directory));
            }
            else
                CloseHandle(lease);
            const auto manifest = FileContents::Read(directory / "manifest.json");
            if (!manifest)
            {
                // 一覧公開前は対象ファイルへ触れないため、準備中の記録だけを回収できます。
                std::filesystem::remove_all(directory);
                return {};
            }
            const auto json = Json::parse(*manifest);
            if ((json.at("version") != 2 && json.at("version") != 3 && json.at("version") != 4) ||
                !json.at("entries").is_array() || json.at("entries").size() > 1024)
                throw std::runtime_error("復旧記録が不正です：" + FileContents::Text(directory));
            std::vector<Entry> entries;
            size_t index = 0;
            for (const auto& value : json.at("entries"))
            {
                Entry entry;
                entry.path = Path(value.at("path").get<std::string>());
                entry.stage = Path(value.at("stage").get<std::string>());
                entry.backup = Path(value.at("backup").get<std::string>());
                entry.beforeId = value.at("beforeId").get<std::string>();
                entry.afterId = value.at("afterId").get<std::string>();
                if (entry.beforeId.empty() == value.at("before").get<bool>() ||
                    entry.afterId.empty() == value.at("after").get<bool>())
                    throw std::runtime_error("復旧記録の実体IDが不正です");
                for (const auto& path : {entry.path, entry.stage, entry.backup})
                    if (!path.is_absolute() || !Inside(root, path))
                        throw std::runtime_error("復旧記録のパスが保存領域外です");
                const auto prefix = entry.path.native() + L".wp1-";
                if (!entry.stage.native().starts_with(prefix) || !entry.stage.native().ends_with(L"-stage") ||
                    !entry.backup.native().starts_with(prefix) || !entry.backup.native().ends_with(L"-backup"))
                    throw std::runtime_error("復旧記録の作業名が不正です");
                const auto name = std::to_string(index++);
                if (value.at("before").get<bool>())
                {
                    entry.before = FileContents::Read(directory / (name + ".before"));
                    if (!entry.before)
                        throw std::runtime_error("復旧用の旧内容がありません");
                }
                if (value.at("after").get<bool>())
                {
                    entry.after = FileContents::Read(directory / (name + ".after"));
                    if (!entry.after)
                        throw std::runtime_error("復旧用の保存候補がありません");
                }
                entries.push_back(std::move(entry));
            }
            const auto marker = FileContents::Read(directory / "committed");
            if (marker && *marker != "committed\n")
                throw std::runtime_error("保存の確定記録が不正です");
            const auto completion = FileContents::Read(directory / "completed");
            if (completion && (!marker || json.at("version") == 2 || *completion != "completed\n"))
                throw std::runtime_error("保存の完了記録が不正です");
            const auto aborted = FileContents::Read(directory / "aborted");
            const auto accepted = FileContents::Read(directory / "accepted");
            if ((aborted && (json.at("version") != 4 || *aborted != "aborted\n")) ||
                (accepted && (json.at("version") != 4 || !completion || aborted || *accepted != "accepted\n")))
                throw std::runtime_error("保存の受理・取消記録が不正です");
            const bool preserve = !aborted && !accepted && (json.at("version") == 4 || marker.has_value());
            const auto close = [](void* handle)
            {
                CloseHandle(handle);
            };
            std::vector<std::unique_ptr<void, decltype(close)>> held;
            for (const auto& entry : entries)
            {
                // 完了後の外部編集や削除は、以前の保存内容を再適用せずそのまま保持します。
                if (aborted || !completion)
                {
                    const bool forward = marker.has_value() && !aborted;
                    const auto& desired = forward ? entry.after : entry.before;
                    const auto& other = forward ? entry.before : entry.after;
                    const auto& desiredId = forward ? entry.afterId : entry.beforeId;
                    const auto& otherId = forward ? entry.beforeId : entry.afterId;
                    const bool retained = Retire(entry.path, directory, desired, other, desiredId, otherId, true);
                    if (desired && !retained)
                    {
                        std::filesystem::create_directories(entry.path.parent_path());
                        FileContents::Write(entry.path, *desired, false);
                    }
                    if (desired)
                    {
                        auto handle = Open(entry.path);
                        std::unique_ptr<void, decltype(close)> guard(handle, close);
                        if (Read(handle) != *desired)
                            throw std::runtime_error("復旧中に対象が変更されました。再試行してください。");
                        held.push_back(std::move(guard));
                    }
                }
                Retire(entry.stage, directory, entry.after, {}, entry.afterId, {});
                Retire(entry.backup, directory, entry.before, {}, entry.beforeId, {});
            }
            std::vector<std::filesystem::path> conflicts;
            for (const auto& entry : std::filesystem::directory_iterator(directory))
                if (entry.path().extension() == ".conflict")
                    conflicts.push_back(entry.path().filename());
            // 受理結果を確定できない記録は、両世代のコピーを保管して掃除から除外します。
            if (preserve)
                conflicts.push_back("manifest.json");
            if (!conflicts.empty())
            {
                auto archived = directory;
                archived.replace_extension(".recovered");
                RenameDirectory(directory, archived);
                for (auto& conflict : conflicts)
                    conflict = archived / conflict;
            }
            else
            {
                // 掃除途中の再起動ではコピー不足を復旧失敗としないよう、先に完了名へ移します。
                auto completed = directory;
                completed.replace_extension(".completed");
                RenameDirectory(directory, completed);
                std::filesystem::remove_all(completed);
            }
            return conflicts;
        }
        std::filesystem::path directory_;
        struct HandleCloser
        {
            /// <summary>保存中の記録の保持を解放します。</summary>
            void operator()(void* handle) const
            {
                CloseHandle(handle);
            }
        };
        std::unique_ptr<void, HandleCloser> lease_;
    };
}
