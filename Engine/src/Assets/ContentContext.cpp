#include <Engine/Assets/ContentContext.h>

namespace Engine
{
    auto ContentContext::Capture(const std::filesystem::path& root) -> std::shared_ptr<const ContentContext>
    {
        auto context = std::shared_ptr<ContentContext>(new ContentContext);
        context->root_ = std::filesystem::weakly_canonical(std::filesystem::absolute(root));
        context->shaders_ = ProjectContext::ShaderRoot(context->root_);
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

    auto ContentContext::RequireRoot(const std::filesystem::path& root) const -> void
    {
        const auto resolved = std::filesystem::weakly_canonical(std::filesystem::absolute(root));
        if (CompareStringOrdinal(resolved.c_str(), -1, root_.c_str(), -1, TRUE) != CSTR_EQUAL)
            throw std::runtime_error("Content context belongs to a different project");
    }

    auto ContentContext::RequireConfiguration() const -> void
    {
        const auto current = Capture(root_);
        if (CompareStringOrdinal(current->shaders_.c_str(), -1, shaders_.c_str(), -1, TRUE) != CSTR_EQUAL ||
            current->identity_ != identity_)
            throw std::runtime_error("Project content declaration changed; reopen the project");
    }

    auto ContentContext::CheckConfiguration(std::string& error) const -> bool
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
}
