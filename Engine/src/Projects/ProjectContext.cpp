#include <Engine/Projects/ProjectContext.h>

namespace Engine
{
    auto ProjectContext::ValidName(const std::string& value) -> bool
    {
        const auto letter = [](unsigned char c)
        {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        };
        return !value.empty() && value.size() <= 64 && letter(value.front()) &&
               std::all_of(value.begin(), value.end(),
                           [&](unsigned char c)
                           {
                               return letter(c) || (c >= '0' && c <= '9') || c == '_';
                           });
    }

    auto ProjectContext::ValidIdentity(const std::string& value) -> bool
    {
        return value.size() == 32 && std::all_of(value.begin(), value.end(),
                                                 [](unsigned char c)
                                                 {
                                                     return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                                                 });
    }

    auto ProjectContext::Path(const std::string& value) -> std::filesystem::path
    {
        if (value.empty() || value.find('\0') != std::string::npos)
            throw std::runtime_error("Empty or invalid project path");
        return std::filesystem::path(std::u8string(value.begin(), value.end()));
    }

    auto ProjectContext::ShaderRoot(const std::filesystem::path& root) -> std::filesystem::path
    {
        std::ifstream input(root / "WP1.project.json");
        if (!input)
            return std::filesystem::weakly_canonical(root / "Engine/Shaders");
        return std::filesystem::weakly_canonical(ResolveEngine(root, Json::parse(input)) / "Engine/Shaders");
    }

    auto ProjectContext::Identity(const std::filesystem::path& root) -> std::string
    {
        std::string identity;
        std::ifstream descriptor(root / "WP1.project.json");
        if (descriptor)
        {
            const auto document = Json::parse(descriptor);
            if (document.contains("projectId"))
            {
                identity = document.at("projectId").get<std::string>();
                if (!ValidIdentity(identity))
                    throw std::runtime_error("Invalid project ID");
            }
        }
        if (identity.empty())
        {
            std::ifstream runtime(root / "WP1.runtime.json");
            if (runtime)
            {
                const auto document = Json::parse(runtime);
                if (!document.at("version").is_number_integer() || document.at("version").get<int>() != 1)
                    throw std::runtime_error("Unsupported runtime project context");
                identity = document.at("projectId").get<std::string>();
                if (!ValidIdentity(identity))
                    throw std::runtime_error("Invalid runtime project ID");
            }
        }
        if (identity.empty())
        {
            std::ifstream metadata(root / "Assets/Project.json.meta");
            if (metadata)
                identity = Json::parse(metadata).at("id").get<std::string>();
        }
        if (!ValidIdentity(identity))
            throw std::runtime_error("Project needs a stable project ID or legacy Project.json asset ID");
        return identity;
    }

    auto ProjectContext::Load(std::filesystem::path path) -> ProjectContext
    {
        path = std::filesystem::absolute(path).lexically_normal();
        if (path.filename() == "WP1.project.json")
            path = path.parent_path();
        std::ifstream input(path / "WP1.project.json");
        if (!input)
            throw std::runtime_error("WP1.project.json is missing");
        const auto descriptor = Json::parse(input);
        if (!descriptor.at("version").is_number_integer() || descriptor.at("version").get<int>() != 1 ||
            (descriptor.contains("engineContract") &&
             (!descriptor.at("engineContract").is_number_integer() ||
              descriptor.at("engineContract").get<unsigned int>() != ContractVersion)))
            throw std::runtime_error("Unsupported project or engine contract version");
        ProjectContext result;
        result.root = std::filesystem::weakly_canonical(path);
        result.engineRoot = ResolveEngine(result.root, descriptor);
        result.name = descriptor.at("name").get<std::string>();
        result.projectId = descriptor.value("projectId", std::string{});
        result.generatedProjects = descriptor.value("generatedProjects", false);
        if (!ValidName(result.name) || descriptor.at("project").get<std::string>() != result.name + ".vcxproj" ||
            descriptor.at("solution").get<std::string>() != result.name + ".slnx" ||
            (descriptor.contains("projectId") && !ValidIdentity(result.projectId)))
            throw std::runtime_error("Invalid game project descriptor");
        return result;
    }

    auto ProjectContext::RequireEngine(const std::filesystem::path& host) const -> void
    {
        std::error_code error;
        if (!std::filesystem::equivalent(engineRoot, host, error) || error)
            throw std::runtime_error("Project uses a different engine checkout; open its matching Editor");
    }
}
