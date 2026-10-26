#pragma once
#include <Engine/Core/FileContents.h>
#include <cwctype>
#include <vector>

namespace Engine
{
    /// <summary>同じ保存領域を使うプロセスとスレッドを、復旧から確定まで直列化します。</summary>
    class FileSaveLock final
    {
    public:
        /// <summary>アセットはプロジェクト単位、それ以外は親フォルダー単位で保護します。</summary>
        static std::filesystem::path Root(const std::filesystem::path& file);
        /// <summary>保存記録を通常の素材やフォルダーとして索引へ混ぜないために識別します。</summary>
        static bool IsJournalPath(const std::filesystem::path& path);
        /// <summary>異常終了で放棄された排他も取得し、呼出元に復旧の機会を与えます。</summary>
        explicit FileSaveLock(const std::filesystem::path& root);
        /// <summary>保存の排他は複製しません。</summary>
        FileSaveLock(const FileSaveLock&) = delete;
        /// <summary>保存の排他は代入しません。</summary>
        FileSaveLock& operator=(const FileSaveLock&) = delete;
        /// <summary>同じスレッドの入れ子を解消して排他を解放します。</summary>
        ~FileSaveLock();
        /// <summary>入れ子の保存中に、その保存を復旧対象にしないために使います。</summary>
        bool Outermost() const
        {
            return outermost_;
        }

    private:
        inline static thread_local std::vector<std::wstring> held_;
        std::wstring key_;
        HANDLE mutex_ = nullptr;
        bool outermost_ = false;
    };
}
