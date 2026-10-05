#pragma once
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace Editor
{
    enum class AssetKind { Model, Scene };
    struct ProjectAsset
    {
        std::filesystem::path path;
        AssetKind kind = AssetKind::Model;
    };
    class ProjectCatalog final
    {
    public:
        static std::string Text(const std::filesystem::path& path)
        {
            const auto utf8=path.generic_u8string();
            return {utf8.begin(),utf8.end()};
        }
        bool Scan(const std::filesystem::path& root)
        {
            try
            {
                std::vector<ProjectAsset> assets;
                std::set<std::filesystem::path> folders{"Assets"};
                for (const auto& entry : std::filesystem::recursive_directory_iterator(root/"Assets"))
                {
                    if (!entry.is_regular_file()) continue;
                    const auto relative=entry.path().lexically_relative(root);
                    const auto extension=Lower(Text(relative.extension()));
                    const bool scene=extension==".json" && Text(relative).starts_with("Assets/Scenes/");
                    if (extension!=".obj" && !scene) continue;
                    assets.push_back({relative, scene ? AssetKind::Scene : AssetKind::Model});
                    for (auto parent=relative.parent_path(); !parent.empty(); parent=parent.parent_path()) folders.insert(parent);
                }
                std::sort(assets.begin(),assets.end(), [](const auto& a,const auto& b) { return a.path<b.path; });
                assets_=std::move(assets);
                folders_={folders.begin(),folders.end()};
                error_.clear();
                return true;
            }
            catch (const std::exception& error) { error_=error.what(); return false; }
        }
        static bool Matches(const ProjectAsset& asset, const std::filesystem::path& folder, const std::string& search)
        {
            if (search.empty()) return asset.path.parent_path()==folder;
            return Lower(Text(asset.path)).find(Lower(search))!=std::string::npos;
        }
        const std::vector<ProjectAsset>& Assets() const { return assets_; }
        const std::vector<std::filesystem::path>& Folders() const { return folders_; }
        const std::string& Error() const { return error_; }
    private:
        static std::string Lower(std::string value)
        {
            std::transform(value.begin(),value.end(),value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return value;
        }
        std::vector<ProjectAsset> assets_;
        std::vector<std::filesystem::path> folders_;
        std::string error_;
    };
}
