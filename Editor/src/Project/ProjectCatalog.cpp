#include "ProjectCatalog.h"

namespace Editor
{
    auto ProjectCatalog::Scan(const std::filesystem::path& root) -> bool
    {
        try
        {
            Engine::FileSaveLock saveLock(root);
            if (saveLock.Outermost())
                Engine::FileJournal::RecoverLocked(root);
            auto schemas = SceneRuntime::ScriptSchema::Builtins();
            for (auto& [name, schema] : SceneRuntime::ScriptSchema::LoadProject(root))
                if (!schemas.emplace(name, std::move(schema)).second)
                    throw std::runtime_error("Project cannot replace a builtin script schema: " + name);
            std::vector<ProjectAsset> assets;
            std::set<std::filesystem::path> folders{"Assets"};
            ScanFolder(root, "Assets", assets, folders);
            if (std::filesystem::is_directory(root / "Shaders"))
                ScanFolder(root, "Shaders", assets, folders);
            std::sort(assets.begin(), assets.end(),
                      [](const auto& a, const auto& b)
                      {
                          return a.path < b.path;
                      });
            static_cast<void>(Engine::AssetDatabase(root));
            schemas_ = std::move(schemas);
            root_ = root;
            assets_ = std::move(assets);
            folders_ = {folders.begin(), folders.end()};
            children_.clear();
            for (const auto& folder : folders_)
                children_[folder.parent_path()].push_back(folder);
            error_.clear();
            ++revision_;
            return true;
        }
        catch (const std::exception& error)
        {
            error_ = error.what();
            return false;
        }
    }

    auto ProjectCatalog::Kind(const std::filesystem::path& path) -> std::optional<AssetKind>
    {
        const auto extension = Lower(Text(path.extension()));
        if (extension == ".mat" && Text(path).starts_with("Assets/Materials/"))
            return AssetKind::Material;
        if (extension == ".prefab" && Text(path).starts_with("Assets/Prefabs/"))
            return AssetKind::Prefab;
        if (extension == ".obj" || extension == ".gltf" || extension == ".glb")
            return AssetKind::Model;
        if (extension == ".json" && Text(path).starts_with("Assets/Scenes/"))
            return AssetKind::Scene;
        if (HasExtension(extension, {".png", ".jpg", ".jpeg", ".bmp", ".tif", ".tiff", ".dds"}))
            return AssetKind::Texture;
        if (HasExtension(extension, {".wav", ".mp3", ".aac", ".m4a", ".ogg", ".flac"}))
            return AssetKind::Audio;
        if (HasExtension(extension, {".hlsl", ".hlsli"}))
            return AssetKind::Shader;
        if (HasExtension(extension, {".ttf", ".otf"}))
            return AssetKind::Font;
        return std::nullopt;
    }

    auto ProjectCatalog::Label(AssetKind kind) -> const char*
    {
        switch (kind)
        {
        case AssetKind::Model:
            return "モデル";
        case AssetKind::Scene:
            return "シーン";
        case AssetKind::Texture:
            return "画像";
        case AssetKind::Audio:
            return "音声";
        case AssetKind::Shader:
            return "シェーダー";
        case AssetKind::Font:
            return "フォント";
        case AssetKind::Prefab:
            return "Prefab";
        case AssetKind::Material:
            return "Material";
        }
        return "アセット";
    }

    auto ProjectCatalog::Matches(const ProjectAsset& asset, const std::filesystem::path& folder,
                                 const std::string& search) -> bool
    {
        if (search.empty())
            return asset.path.parent_path() == folder;
        return Lower(Text(asset.path)).find(Lower(search)) != std::string::npos;
    }

    auto ProjectCatalog::Children(const std::filesystem::path& folder) const
        -> const std::vector<std::filesystem::path>&
    {
        static const std::vector<std::filesystem::path> empty;
        const auto found = children_.find(folder);
        return found == children_.end() ? empty : found->second;
    }

    auto ProjectCatalog::ScanFolder(const std::filesystem::path& root, const std::filesystem::path& folder,
                                    std::vector<ProjectAsset>& assets, std::set<std::filesystem::path>& folders) -> void
    {
        folders.insert(folder);
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root / folder))
        {
            if (Engine::FileSaveLock::IsJournalPath(entry.path().lexically_relative(root)))
                continue;
            if (entry.is_directory())
            {
                folders.insert(entry.path().lexically_relative(root));
                continue;
            }
            if (!entry.is_regular_file())
                continue;
            const auto relative = entry.path().lexically_relative(root);
            const auto kind = Kind(relative);
            if (!kind)
                continue;
            Engine::AssetDatabase::Ensure(entry.path());
            assets.push_back({relative, *kind});
            for (auto parent = relative.parent_path(); !parent.empty(); parent = parent.parent_path())
                folders.insert(parent);
        }
    }

    auto ProjectCatalog::Lower(std::string value) -> std::string
    {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c)
                       {
                           return static_cast<char>(std::tolower(c));
                       });
        return value;
    }
}
