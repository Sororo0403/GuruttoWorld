#include <Engine/Assets/ContentFiles.h>

namespace Engine
{
    auto ContentFiles::Root(std::filesystem::path path) -> std::filesystem::path
    {
        path = std::filesystem::absolute(path).lexically_normal().parent_path();
        while (!path.empty() && path != path.root_path())
        {
            if (path.filename() == "Assets" || path.filename() == "Shaders")
            {
                const auto parent = path.parent_path();
                if (std::filesystem::is_regular_file(parent / "WP1.project.json") ||
                    std::filesystem::is_regular_file(parent / "WP1.runtime.json"))
                    return parent;
                return parent.filename() == "Engine" ? parent.parent_path() : parent;
            }
            path = path.parent_path();
        }
        return {};
    }

    auto ContentFiles::Resolve(const std::filesystem::path& path, const std::filesystem::path& root,
                               const std::filesystem::path& shaders) -> std::filesystem::path
    {
        const auto absolute = std::filesystem::absolute(path).lexically_normal();
        const auto relative = absolute.lexically_relative(root);
        if (IsEngineResource(relative))
            return shaders / relative.lexically_relative("Engine/Shaders");
        if (!std::filesystem::is_regular_file(absolute) && relative.generic_string().starts_with("Shaders/"))
        {
            // ゲーム自身のシェーダーには標準リソースのメタデータを流用しません。
            auto asset = absolute;
            if (asset.extension() == ".meta")
            {
                asset.replace_extension();
                if (std::filesystem::is_regular_file(asset))
                    return absolute;
            }
            const auto legacy = shaders / relative.lexically_relative("Shaders");
            if (std::filesystem::is_regular_file(legacy))
                return legacy;
        }
        return absolute;
    }

    auto ContentFiles::Resolve(const std::filesystem::path& path) -> std::filesystem::path
    {
        const auto absolute = std::filesystem::absolute(path).lexically_normal();
        const auto root = Root(absolute);
        return root.empty() ? absolute : Resolve(absolute, root, ProjectContext::ShaderRoot(root));
    }

    auto ContentFiles::List(const std::filesystem::path& root) -> std::map<std::filesystem::path, std::filesystem::path>
    {
        std::map<std::filesystem::path, std::filesystem::path> files;
        for (const auto* folder : {"Assets", "Shaders"})
        {
            if (!std::filesystem::is_directory(root / folder))
                continue;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(root / folder))
                if (entry.is_regular_file())
                    files[entry.path().lexically_relative(root)] = entry.path();
        }
        return files;
    }
}
