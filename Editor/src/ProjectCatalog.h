#pragma once
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>
#include <string>
#include <vector>
#include <optional>
#include <initializer_list>
#include <string_view>

namespace Editor
{
    enum class AssetKind { Model, Scene, Texture, Audio, Shader, Font, Prefab };
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
                ScanFolder(root,"Assets",assets,folders);
                if (std::filesystem::is_directory(root/"Shaders")) ScanFolder(root,"Shaders",assets,folders);
                std::sort(assets.begin(),assets.end(), [](const auto& a,const auto& b) { return a.path<b.path; });
                assets_=std::move(assets);
                folders_={folders.begin(),folders.end()};
                error_.clear();
                return true;
            }
            catch (const std::exception& error) { error_=error.what(); return false; }
        }
        static std::optional<AssetKind> Kind(const std::filesystem::path& path)
        {
            const auto extension=Lower(Text(path.extension()));
            if (extension==".prefab" && Text(path).starts_with("Assets/Prefabs/")) return AssetKind::Prefab;
            if (extension==".obj") return AssetKind::Model;
            if (extension==".json" && Text(path).starts_with("Assets/Scenes/")) return AssetKind::Scene;
            if (HasExtension(extension,{".png",".jpg",".jpeg",".bmp",".tif",".tiff",".dds"})) return AssetKind::Texture;
            if (HasExtension(extension,{".wav",".mp3",".aac",".m4a",".ogg",".flac"})) return AssetKind::Audio;
            if (HasExtension(extension,{".hlsl",".hlsli"})) return AssetKind::Shader;
            if (HasExtension(extension,{".ttf",".otf"})) return AssetKind::Font;
            return std::nullopt;
        }
        static const char* Label(AssetKind kind)
        {
            switch (kind)
            {
            case AssetKind::Model: return "モデル";
            case AssetKind::Scene: return "シーン";
            case AssetKind::Texture: return "画像";
            case AssetKind::Audio: return "音声";
            case AssetKind::Shader: return "シェーダー";
            case AssetKind::Font: return "フォント";
            case AssetKind::Prefab: return "Prefab";
            }
            return "アセット";
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
        static bool HasExtension(const std::string& extension, std::initializer_list<std::string_view> supported)
        { return std::find(supported.begin(),supported.end(),extension)!=supported.end(); }
        static void ScanFolder(const std::filesystem::path& root, const std::filesystem::path& folder,
            std::vector<ProjectAsset>& assets, std::set<std::filesystem::path>& folders)
        {
            folders.insert(folder);
            for (const auto& entry : std::filesystem::recursive_directory_iterator(root/folder))
            {
                if (!entry.is_regular_file()) continue;
                const auto relative=entry.path().lexically_relative(root);
                const auto kind=Kind(relative);
                if (!kind) continue;
                assets.push_back({relative,*kind});
                for (auto parent=relative.parent_path(); !parent.empty(); parent=parent.parent_path()) folders.insert(parent);
            }
        }
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
