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
                        const std::optional<std::string>& after);
        /// <summary>保持中のファイルは複製しません。</summary>
        FileReplacement(const FileReplacement&) = delete;
        /// <summary>保持中のファイルは代入しません。</summary>
        FileReplacement& operator=(const FileReplacement&) = delete;
        /// <summary>未確定なら復元し、復元競合時の旧ファイルはバックアップ名で残します。</summary>
        ~FileReplacement();
        /// <summary>保持した旧ファイルを退避し、空いた名前へ非上書きで候補を設置します。</summary>
        void Apply();
        /// <summary>所有する候補だけを退かし、外部で作成されたファイルを上書きせず復元します。</summary>
        void Restore();
        /// <summary>全対象の保存成功後に候補を保持し、旧内容を解放可能にします。</summary>
        void Commit() noexcept
        {
            committed_ = true;
        }
        /// <summary>保存領域を復旧してから内容を構築し、保存直前の外部変更も照合します。</summary>
        static void Save(const std::filesystem::path& path, const std::function<std::string()>& prepare,
                         bool overwrite = true);
        /// <summary>単一ファイルにも比較・排他・復元を共通適用します。</summary>
        static void Write(const std::filesystem::path& path, const std::optional<std::string>& before,
                          std::string after);
        /// <summary>全対象の復旧記録を公開してから変更し、失敗時は外部内容も保管して旧版へ戻します。</summary>
        static bool ApplyAll(const std::vector<Change>& changes, std::string& error,
                             const std::function<bool()>& commitDocument);

    private:
        /// <summary>排他を取得し、全作業ファイルと記録を準備してから置換します。</summary>
        static void PrepareBatch(const std::vector<Change>& changes, SaveBatch& batch);
        /// <summary>失敗時の取消方針を同期し、対象の保持を解放してから記録を掃除します。</summary>
        static bool FinishBatch(SaveBatch& batch, std::string& error);
        /// <summary>同じフォルダーに衝突しない作業名を生成します。</summary>
        static std::filesystem::path Unique(std::filesystem::path path, const wchar_t* kind);
        /// <summary>他の書込・削除・置換を拒否し、改名にも同じハンドルを使用します。</summary>
        static HANDLE Open(const std::filesystem::path& path);
        /// <summary>保持した同じファイルの内容を比較用に読み取ります。</summary>
        static std::string Read(HANDLE file);
        /// <summary>他のファイルを上書きせず、保持中の実体だけを改名します。</summary>
        static void Rename(HANDLE file, const std::filesystem::path& destination);
        /// <summary>自分が保持する不要な作業ファイルだけを削除対象にします。</summary>
        static void Delete(HANDLE file) noexcept;
        /// <summary>保持を解放します。</summary>
        static void Close(HANDLE& file) noexcept;
        /// <summary>対象とOSの失敗理由を保持します。</summary>
        [[noreturn]] static void Fail(const char* operation, const std::filesystem::path& path);
        std::filesystem::path path_, stagedPath_, backupPath_;
        HANDLE original_ = INVALID_HANDLE_VALUE, staged_ = INVALID_HANDLE_VALUE;
        bool originalMoved_ = false, installed_ = false, committed_ = false;
    };
}
