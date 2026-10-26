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
        static std::string Identity(HANDLE handle);
        /// <summary>変更先を含む保存領域を求めます。</summary>
        static std::filesystem::path Root(const std::vector<Entry>& entries);
        /// <summary>元のパスを変更する前に、復旧に必要な全コピーと一覧を永続化します。</summary>
        FileJournal(const std::filesystem::path& root, const std::vector<Entry>& entries);
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
        static std::vector<std::filesystem::path> Recover(const std::filesystem::path& root);
        /// <summary>保存領域の排他中に旧保存領域も毎回探索し、遅れて現れた記録も使用前に回復します。</summary>
        static std::vector<std::filesystem::path> RecoverLocked(const std::filesystem::path& root);

    private:
        struct HandleCloser
        {
            /// <summary>保存中の記録の保持を解放します。</summary>
            void operator()(void* handle) const
            {
                CloseHandle(handle);
            }
        };
        /// <summary>旧実装の保存領域を再探索し、探索後に追加された記録を見落とさないようにします。</summary>
        static std::vector<std::filesystem::path> SaveRoots(const std::filesystem::path& root);
        /// <summary>復旧の完了名を非上書きで公開し、一時的な共有違反だけを短く再試行します。</summary>
        static void RenameDirectory(const std::filesystem::path& from, const std::filesystem::path& to);
        /// <summary>同じプロセスでも保存ごとに異なる記録名を作ります。</summary>
        static std::string Token();
        /// <summary>UTF-8で保存したパスを復元します。</summary>
        static std::filesystem::path Path(const std::string& text)
        {
            return std::filesystem::path(std::u8string(text.begin(), text.end()));
        }
        /// <summary>保存領域の外や、再解析によって外へ移されたパスを拒否します。</summary>
        static bool Inside(const std::filesystem::path& root, const std::filesystem::path& path);
        /// <summary>復旧用フォルダーとファイルを別の場所へ転送する入力は拒否します。</summary>
        static void RejectReparse(const std::filesystem::path& path);
        /// <summary>書込と改名を排他した、通常ファイルだけを保持します。</summary>
        static HANDLE Open(const std::filesystem::path& path);
        /// <summary>保持した実体を比較し、同じ名前への差し替え競合を避けます。</summary>
        static std::string Read(HANDLE handle);
        /// <summary>保持した実体だけを、他の内容を上書きせずに移動します。</summary>
        static void Move(HANDLE handle, const std::filesystem::path& to);
        /// <summary>現在の内容を保存し、外部版の場合は掃除しない保管先に分類します。</summary>
        static bool Retire(const std::filesystem::path& path, const std::filesystem::path& directory,
                           const std::optional<std::string>& first, const std::optional<std::string>& second,
                           const std::string& firstId, const std::string& secondId, bool keepIfDesired = false);
        struct RecoveryState
        {
            bool forward, restore, preserve;
        };
        /// <summary>受理と取消の記録が完了境界と矛盾しないことを確認します。</summary>
        static void ValidateOutcome(const Json& json, const std::optional<std::string>& completion,
                                    const std::optional<std::string>& aborted,
                                    const std::optional<std::string>& accepted);
        /// <summary>記録された確定・完了・取消の組合せを検証します。</summary>
        static RecoveryState ReadRecoveryState(const std::filesystem::path& directory, const Json& json);
        /// <summary>対象と作業ファイルを回復し、掃除が終わるまで対象を保持します。</summary>
        static std::vector<std::unique_ptr<void, HandleCloser>> RecoverEntries(const std::filesystem::path& directory,
                                                                               const std::vector<Entry>& entries,
                                                                               const RecoveryState& state);
        /// <summary>実体IDと作業名が対象の保存領域に属することを確認します。</summary>
        static void ValidateEntry(const Entry& entry, const Json& value, const std::filesystem::path& root);
        /// <summary>復旧先の領域と全世代のコピーを、書き戻し前に検証します。</summary>
        static std::vector<Entry> ReadEntries(const std::filesystem::path& root, const std::filesystem::path& directory,
                                              const Json& json);
        /// <summary>全コピーを先に検証してから、確定記録に従って回復を再開します。</summary>
        static std::vector<std::filesystem::path> RecoverOne(const std::filesystem::path& root,
                                                             const std::filesystem::path& directory);
        std::filesystem::path directory_;
        std::unique_ptr<void, HandleCloser> lease_;
    };
}
