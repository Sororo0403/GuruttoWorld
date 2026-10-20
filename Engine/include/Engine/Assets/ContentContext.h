#pragma once
#include <Engine/Projects/ProjectContext.h>
#include <Engine/Core/FileContents.h>
#include <memory>
#include <vector>

namespace Engine
{
    class ContentContext final
    {
    public:
        /// <summary>所有者の開始時に参照先とIDを一度だけ解決します。設定ファイルへ書き込みません。</summary>
        static std::shared_ptr<const ContentContext> Capture(const std::filesystem::path& root)
        {
            auto context = std::shared_ptr<ContentContext>(new ContentContext);
            context->root_ = std::filesystem::weakly_canonical(std::filesystem::absolute(root));
            context->shared_ = ProjectContext::SharedRoot(context->root_);
            if (!context->shared_.empty() && context->shared_ != context->root_)
                context->layers_.push_back(context->shared_);
            context->layers_.push_back(context->root_);
            if (std::filesystem::is_regular_file(context->root_ / "WP1.runtime.json") ||
                std::filesystem::is_regular_file(context->root_ / "Assets/Project.json.meta"))
                context->identity_ = ProjectContext::Identity(context->root_);
            else if (const auto descriptor = FileContents::Read(context->root_ / "WP1.project.json"))
            {
                const auto json = Json::parse(*descriptor);
                if (json.contains("projectId"))
                {
                    context->identity_ = json.at("projectId").get<std::string>();
                    if (!ProjectContext::ValidIdentity(context->identity_))
                        throw std::runtime_error("Invalid project ID");
                }
            }
            return context;
        }
        /// <summary>固定したプロジェクトの絶対パスを返します。</summary>
        const std::filesystem::path& Root() const
        {
            return root_;
        }
        /// <summary>固定した共有素材の絶対パスを返します。</summary>
        const std::filesystem::path& SharedRoot() const
        {
            return shared_;
        }
        /// <summary>固定した素材層を優先順位順に返します。</summary>
        const std::vector<std::filesystem::path>& Layers() const
        {
            return layers_;
        }
        /// <summary>固定した設定保存先のIDを返します。素材だけの検証環境では空です。</summary>
        const std::string& ProjectId() const
        {
            return identity_;
        }
        /// <summary>呼出元のルートが所有者と一致することを検証します。</summary>
        void RequireRoot(const std::filesystem::path& root) const
        {
            const auto resolved = std::filesystem::weakly_canonical(std::filesystem::absolute(root));
            if (CompareStringOrdinal(resolved.c_str(), -1, root_.c_str(), -1, TRUE) != CSTR_EQUAL)
                throw std::runtime_error("Content context belongs to a different project");
        }
        /// <summary>下位ローダーが宣言を読み直す前に、異なる参照先の混在を拒否します。</summary>
        void RequireConfiguration() const
        {
            const auto current = Capture(root_);
            if (CompareStringOrdinal(current->shared_.c_str(), -1, shared_.c_str(), -1, TRUE) != CSTR_EQUAL ||
                current->identity_ != identity_)
                throw std::runtime_error("Project content declaration changed; reopen the project");
        }
        /// <summary>参照宣言の変更を、所有者を変えずに診断として返します。</summary>
        bool CheckConfiguration(std::string& error) const
        {
            try
            {
                RequireConfiguration();
                return true;
            }
            catch (const std::exception& failure)
            {
                error = failure.what();
                return false;
            }
        }
        /// <summary>固定した共有層からシェーダー・フォントを解決します。</summary>
        std::filesystem::path Resolve(const std::filesystem::path& path) const
        {
            const auto absolute = path.is_absolute() ? path.lexically_normal() : (root_ / path).lexically_normal();
            if (std::filesystem::is_regular_file(absolute))
                return absolute;
            const auto relative = absolute.lexically_relative(root_);
            const auto text = relative.generic_string();
            if (!shared_.empty() && (text.starts_with("Shaders/") || text.starts_with("Assets/Fonts/")) &&
                std::filesystem::is_regular_file(shared_ / relative))
                return shared_ / relative;
            return absolute;
        }

    private:
        std::filesystem::path root_, shared_;
        std::vector<std::filesystem::path> layers_;
        std::string identity_;
    };
}
