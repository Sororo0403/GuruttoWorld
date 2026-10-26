#include "AssetDependencies.h"

namespace Editor
{
    auto AssetDependencies::Refresh(const std::filesystem::path& root, const ProjectAsset& asset,
                                    std::chrono::steady_clock::time_point now) -> void
    {
        const auto path = Engine::ContentFiles::Resolve(root / asset.path);
        if (path == path_ && now < next_)
            return;
        next_ = now + std::chrono::milliseconds(500);
        try
        {
            const auto revision = AssetRevision::Capture(path);
            if (path == path_ && loaded_ && revision == revision_)
                return;
            path_ = path;
            loaded_ = false;
            values_.clear();
            error_.clear();
            auto extension = ProjectCatalog::Text(path.extension());
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char c)
                           {
                               return static_cast<char>(std::tolower(c));
                           });
            const bool json =
                asset.kind == AssetKind::Scene || asset.kind == AssetKind::Prefab || asset.kind == AssetKind::Material;
            // バイナリアセットには対応するテキスト依存形式がないため、内容を読み取りません。
            if (json || extension == ".gltf" || extension == ".obj")
            {
                std::ifstream input(path, std::ios::binary);
                if (!input)
                    throw std::runtime_error("アセットを読み込めません");
                const auto values = ReadDependencies(input, json, extension);
                values_ = {values.begin(), values.end()};
                ++reads_;
            }
            revision_ = revision;
            loaded_ = true;
        }
        catch (const std::exception& error)
        {
            path_ = path;
            loaded_ = false;
            values_.clear();
            error_ = error.what();
        }
    }

    auto AssetDependencies::CollectReferences(const Engine::Json& value, std::set<std::string>& values) -> void
    {
        if (value.is_string())
        {
            const auto text = value.get<std::string>();
            if (Engine::AssetDatabase::Valid(Engine::AssetDatabase::Path(text)))
                values.insert(text);
        }
        else if (value.is_array() || value.is_object())
            for (const auto& item : value)
                CollectReferences(item, values);
    }

    auto AssetDependencies::ReadDependencies(std::istream& input, bool json, const std::string& extension)
        -> std::set<std::string>
    {
        std::set<std::string> values;
        if (json)
        {
            const auto document = Engine::Json::parse(input);
            CollectReferences(document, values);
        }
        else if (extension == ".gltf")
        {
            const auto document = Engine::Json::parse(input);
            for (const auto* key : {"buffers", "images"})
                if (document.contains(key))
                    for (const auto& resource : document[key])
                    {
                        const auto uri = resource.value("uri", std::string{});
                        if (!uri.empty() && !uri.starts_with("data:"))
                            values.insert(uri);
                    }
        }
        else
        {
            std::string line;
            while (std::getline(input, line))
            {
                std::istringstream tokens(line);
                std::string keyword;
                tokens >> keyword;
                if (keyword == "mtllib")
                {
                    std::string name;
                    while (tokens >> name)
                        values.insert(name);
                }
            }
        }
        return values;
    }
}
