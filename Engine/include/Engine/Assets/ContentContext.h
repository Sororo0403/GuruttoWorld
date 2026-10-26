#pragma once
#include <Engine/Assets/ContentFiles.h>
#include <Engine/Core/FileContents.h>
#include <memory>
#include <vector>

namespace Engine
{
    class ContentContext final
    {
    public:
        /// <summary>所有者の開始時に参照先とIDを一度だけ解決します。設定ファイルへ書き込みません。</summary>
        static std::shared_ptr<const ContentContext> Capture(const std::filesystem::path& root);
        /// <summary>固定したプロジェクトの絶対パスを返します。</summary>
        const std::filesystem::path& Root() const
        {
            return root_;
        }
        /// <summary>固定した標準シェーダーのディレクトリを返します。</summary>
        const std::filesystem::path& ShaderRoot() const
        {
            return shaders_;
        }
        /// <summary>固定した設定保存先のIDを返します。素材だけの検証環境では空です。</summary>
        const std::string& ProjectId() const
        {
            return identity_;
        }
        /// <summary>呼出元のルートが所有者と一致することを検証します。</summary>
        void RequireRoot(const std::filesystem::path& root) const;
        /// <summary>下位ローダーが宣言を読み直す前に、異なる参照先の混在を拒否します。</summary>
        void RequireConfiguration() const;
        /// <summary>参照宣言の変更を、所有者を変えずに診断として返します。</summary>
        bool CheckConfiguration(std::string& error) const;
        /// <summary>ゲームの素材と標準シェーダーを、固定した所有者から解決します。</summary>
        std::filesystem::path Resolve(const std::filesystem::path& path) const
        {
            const auto absolute = path.is_absolute() ? path.lexically_normal() : (root_ / path).lexically_normal();
            return ContentFiles::Resolve(absolute, root_, shaders_);
        }

    private:
        std::filesystem::path root_, shaders_;
        std::string identity_;
    };
}
