#include <Engine/Assets/ModelImportInputs.h>
#include <Engine/Assets/AssetDatabase.h>
#include <set>
#include <iomanip>
#include <cstring>

namespace
{
    std::string DecodeUri(std::string value)
    {
        if (value.starts_with("data:"))
            return {};
        std::string result;
        for (size_t index = 0; index < value.size(); ++index)
        {
            if (value[index] == '%')
            {
                if (index + 2 >= value.size())
                    throw std::runtime_error("Incomplete model URI escape");
                const auto digit = [](char character)
                {
                    if (character >= '0' && character <= '9')
                        return character - '0';
                    if (character >= 'a' && character <= 'f')
                        return character - 'a' + 10;
                    if (character >= 'A' && character <= 'F')
                        return character - 'A' + 10;
                    throw std::runtime_error("Invalid model URI escape");
                };
                const auto character = static_cast<char>(digit(value[index + 1]) * 16 + digit(value[index + 2]));
                if (!character)
                    throw std::runtime_error("NUL in model URI");
                result += character;
                index += 2;
            }
            else
                result += value[index];
        }
        return result;
    }
    class Inputs final
    {
    public:
        std::map<std::filesystem::path, std::optional<std::string>> files;
        std::map<std::filesystem::path, std::filesystem::path> textures;
        std::set<std::filesystem::path> textureFiles;
        void Read(const std::filesystem::path& input, bool texture = false)
        {
            const auto path = std::filesystem::absolute(input).lexically_normal();
            if (texture)
            {
                auto canonical = path;
                const auto root = Engine::AssetDatabase::Root(path);
                if (!root.empty())
                    try
                    {
                        canonical = root / Engine::AssetDatabase(root, true).Resolve(path.lexically_relative(root));
                    }
                    catch (const std::exception&)
                    {
                    }
                textures.insert_or_assign(path, canonical);
                textureFiles.insert(path);
                textureFiles.insert(canonical);
                if (canonical != path)
                    Read(canonical);
            }
            if (files.contains(path))
                return;
            const auto bytes = Engine::FileContents::Read(path);
            files.emplace(path, bytes);
            auto meta = path;
            meta += L".meta";
            files.emplace(meta, Engine::FileContents::Read(meta));
            if (!bytes)
                return;
            auto extension = path.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char c)
                           {
                               return static_cast<char>(std::tolower(c));
                           });
            if (extension == ".gltf" || extension == ".glb")
            {
                std::string_view json = *bytes;
                if (extension == ".glb")
                {
                    const auto word = [&](size_t offset)
                    {
                        std::uint32_t value;
                        if (offset > bytes->size() || bytes->size() - offset < sizeof(value))
                            throw std::runtime_error("Incomplete GLB chunk");
                        std::memcpy(&value, bytes->data() + offset, sizeof(value));
                        return value;
                    };
                    if (word(0) != 0x46546c67 || word(4) != 2 || word(8) != bytes->size() || word(16) != 0x4e4f534a ||
                        word(12) > bytes->size() - 20)
                        throw std::runtime_error("Invalid GLB JSON chunk");
                    json = std::string_view(*bytes).substr(20, word(12));
                }
                const auto document = Engine::Json::parse(json);
                for (const auto* group : {"buffers", "images"})
                    if (document.contains(group))
                        for (const auto& item : document.at(group))
                            if (item.contains("uri"))
                            {
                                const auto raw = item.at("uri").get<std::string>();
                                const auto uri = DecodeUri(raw);
                                if (!uri.empty())
                                {
                                    // 固定版Assimpは生のURI名も開くため、符号化名と復号名の両方を追跡します。
                                    Read(path.parent_path() / Engine::AssetPath::Path(raw),
                                         std::string_view(group) == "images");
                                    if (raw != uri)
                                        Read(path.parent_path() / Engine::AssetPath::Path(uri),
                                             std::string_view(group) == "images");
                                }
                            }
            }
            else if (extension == ".obj" || extension == ".mtl")
            {
                std::istringstream lines(*bytes);
                for (std::string line; std::getline(lines, line);)
                {
                    std::istringstream tokens(line);
                    std::string command;
                    tokens >> command;
                    if (extension == ".obj" && command == "mtllib")
                    {
                        std::string file;
                        std::getline(tokens >> std::ws, file);
                        if (!file.empty() && file.back() == '\r')
                            file.pop_back();
                        if (!file.empty())
                            Read(path.parent_path() / Engine::AssetPath::Path(file));
                        auto fallback = path;
                        fallback.replace_extension(".mtl");
                        Read(fallback);
                    }
                    else if (extension == ".mtl" &&
                             (command.starts_with("map_") || command == "bump" || command == "disp" ||
                              command == "decal" || command == "refl" || command == "norm"))
                    {
                        while (tokens >> std::ws && tokens.peek() == '-')
                        {
                            std::string option, ignored;
                            tokens >> option;
                            size_t arguments = option == "-mm"                                        ? 2
                                               : (option == "-o" || option == "-s" || option == "-t") ? 3
                                               : (option == "-clamp" || option == "-type" || option == "-bm" ||
                                                  option == "-blendu" || option == "-blendv" || option == "-boost" ||
                                                  option == "-texres" || option == "-imfchan")
                                                   ? 1
                                                   : 0;
                            while (arguments-- && tokens >> ignored)
                            {
                            }
                        }
                        std::string file;
                        std::getline(tokens >> std::ws, file);
                        if (!file.empty() && file.back() == '\r')
                            file.pop_back();
                        if (!file.empty())
                            Read(path.parent_path() / Engine::AssetPath::Path(file), true);
                    }
                }
            }
        }
    };
}
namespace Engine
{
    ModelImportInputs ModelImportInputs::Capture(const std::filesystem::path& path, const std::string& kind)
    {
        const auto source = std::filesystem::absolute(path).lexically_normal();
        Inputs inputs;
        inputs.Read(source);
        if (!inputs.files.at(source))
            throw std::runtime_error("Model source missing: " + AssetPath::Text(source));
        const auto metadata = AssetDatabase::Read(source);
        const auto root = AssetDatabase::Root(source);
        const auto identity =
            metadata.id.empty()
                ? "path:" + AssetPath::Text(root.empty() ? source.filename() : source.lexically_relative(root))
                : metadata.id;
        ModelImportInputs result;
        result.descriptor = {kind,
                             identity,
                             ArtifactHash::Bytes(*inputs.files.at(source)),
                             kind == "static-mesh" ? "Assimp/e0b52347c6e52de2827ec957a9ebf00ce3c54f79/WP1-static1"
                                                   : "Assimp/e0b52347c6e52de2827ec957a9ebf00ce3c54f79/WP1-rig2",
                             Json{{"scale", metadata.scale}, {"flipV", metadata.flipV}}.dump(),
                             "windows-x64-f32le",
                             "",
                             {}};
        for (const auto& [original, canonical] : inputs.textures)
            result.descriptor.dependencies.push_back(
                {"texture-path:" + AssetPath::Text(original.lexically_relative(source.parent_path())),
                 AssetPath::Text(canonical.lexically_relative(source.parent_path()))});
        std::vector<std::pair<std::string, std::filesystem::path>> files;
        for (const auto& [file, bytes] : inputs.files)
        {
            auto owner = file;
            if (owner.extension() == ".meta")
                owner.replace_extension();
            if (kind == "collision-mesh" && inputs.textureFiles.contains(owner))
                continue;
            if (FileContents::Read(file) != bytes)
                throw std::runtime_error("Model sources changed while discovering dependencies");
            const auto id = file == source ? "source"
                            : file == std::filesystem::path(source.wstring() + L".meta")
                                ? "source.meta"
                                : AssetPath::Text(file.lexically_relative(source.parent_path()));
            files.emplace_back(id, file);
        }
        result.sources = ArtifactSources::Capture(files);
        for (const auto& [id, file] : files)
            if (file.extension() == ".meta" || inputs.textureFiles.contains(file))
                result.sources.ProtectOnly(id);
        for (const auto& [file, bytes] : inputs.files)
            if (FileContents::Read(file) != bytes)
                throw std::runtime_error("Model sources changed while capturing dependencies");
        return result;
    }
}
