#include <Engine/Assets/AssetDatabase.h>
#include <Engine/Core/Profiler.h>
#include <mutex>

namespace
{
    class ChangeNotification final
    {
    public:
        // フォルダーの変更通知を開始し、取得できない場合は再利用を禁止します。
        ChangeNotification(const std::filesystem::path& path, bool recursive, DWORD filter)
            : handle_(FindFirstChangeNotificationW(path.c_str(), recursive, filter))
        {
        }

        // 通知用のハンドルを閉じます。
        ~ChangeNotification()
        {
            if (handle_ != INVALID_HANDLE_VALUE)
                FindCloseChangeNotification(handle_);
        }

        ChangeNotification(const ChangeNotification&) = delete;
        ChangeNotification& operator=(const ChangeNotification&) = delete;

        // 通知エラーも変更として扱い、古い索引の再利用を防ぎます。
        bool Changed() const
        {
            return handle_ == INVALID_HANDLE_VALUE || WaitForSingleObject(handle_, 0) != WAIT_TIMEOUT;
        }

    private:
        HANDLE handle_ = INVALID_HANDLE_VALUE;
    };

    struct LayerOrder
    {
        // 素材層の順序を保ち、Windows のパス表記の大小だけの違いを吸収します。
        bool operator()(const std::vector<std::filesystem::path>& left,
                        const std::vector<std::filesystem::path>& right) const
        {
            return std::lexicographical_compare(left.begin(), left.end(), right.begin(), right.end(),
                                                [](const auto& a, const auto& b)
                                                {
                                                    return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) ==
                                                           CSTR_LESS_THAN;
                                                });
        }
    };

    // 素材フォルダーの内容と、その削除・再作成を監視します。
    std::vector<std::unique_ptr<ChangeNotification>> WatchLayers(const Engine::ContentContext& context)
    {
        std::vector<std::unique_ptr<ChangeNotification>> notifications;
        constexpr DWORD Contents = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                   FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE;
        for (const auto& layer : std::vector<std::filesystem::path>{context.Root()})
        {
            notifications.push_back(std::make_unique<ChangeNotification>(layer, false, FILE_NOTIFY_CHANGE_DIR_NAME));
            for (const auto* folder : {"Assets", "Shaders"})
                if (std::filesystem::is_directory(layer / folder))
                    notifications.push_back(std::make_unique<ChangeNotification>(layer / folder, true, Contents));
        }
        if (std::filesystem::is_directory(context.ShaderRoot()))
            notifications.push_back(std::make_unique<ChangeNotification>(context.ShaderRoot(), true, Contents));
        return notifications;
    }
}

namespace Engine
{
    AssetDatabase::AssetDatabase(std::shared_ptr<const ContentContext> context, bool snapshot)
        : root_(context->Root()), snapshot_(snapshot), context_(std::move(context))
    {
        index_ = AcquireIndex(context_);
    }

    std::shared_ptr<const AssetDatabase::Index> AssetDatabase::AcquireIndex(
        const std::shared_ptr<const ContentContext>& context)
    {
        struct Entry
        {
            std::shared_ptr<const Index> index;
            std::vector<std::unique_ptr<ChangeNotification>> notifications;
            std::uint64_t used = 0;
            bool ready = false;
            bool shadersPresent = false;

            // 変更を観測した索引や監視できない索引は再利用しません。
            bool Changed() const
            {
                return !ready || std::any_of(notifications.begin(), notifications.end(),
                                             [](const auto& notification)
                                             {
                                                 return notification->Changed();
                                             });
            }
        };
        static std::mutex mutex;
        static std::map<std::vector<std::filesystem::path>, Entry, LayerOrder> entries;
        static std::uint64_t sequence = 0;
        const std::lock_guard lock(mutex);
        const std::vector<std::filesystem::path> layers{context->Root(), context->ShaderRoot()};
        if (!entries.contains(layers) && entries.size() >= 64)
        {
            const auto oldest = std::min_element(entries.begin(), entries.end(),
                                                 [](const auto& a, const auto& b)
                                                 {
                                                     return a.second.used < b.second.used;
                                                 });
            entries.erase(oldest);
        }
        auto& entry = entries[layers];
        entry.used = ++sequence;
        const bool shadersPresent = std::filesystem::is_directory(context->ShaderRoot());
        if (entry.Changed() || entry.shadersPresent != shadersPresent)
        {
            // 走査前に通知を登録し、走査中の変更は次回の取得時にも必ず反映します。
            entry.ready = false;
            entry.shadersPresent = shadersPresent;
            entry.notifications = WatchLayers(*context);
            auto index = BuildIndex(*context);
            entry.index = std::move(index);
            entry.ready = std::none_of(entry.notifications.begin(), entry.notifications.end(),
                                       [](const auto& notification)
                                       {
                                           return notification->Changed();
                                       });
        }
        return entry.index;
    }

    std::shared_ptr<const AssetDatabase::Index> AssetDatabase::BuildIndex(const ContentContext& context)
    {
        CpuScope scope("Asset index rebuild");
        auto index = std::make_shared<Index>();

        for (const auto& layer : std::vector<std::filesystem::path>{context.Root()})
        {
            for (const auto* folder : {"Assets", "Shaders"})
            {
                if (!std::filesystem::is_directory(layer / folder))
                    continue;
                for (const auto& entry : std::filesystem::recursive_directory_iterator(layer / folder))
                {
                    if (FileSaveLock::IsJournalPath(entry.path().lexically_relative(layer)))
                        continue;
                    if (!entry.is_regular_file() || entry.path().extension() != ".meta")
                        continue;
                    auto source = entry.path();
                    source.replace_extension();
                    if (!std::filesystem::is_regular_file(source))
                        continue;
                    const auto rawMetadata = FileContents::Read(Sidecar(source));
                    if (!rawMetadata)
                        throw std::runtime_error("Asset metadata disappeared: " + Text(source));
                    const auto metadata = ParseMetadata(*rawMetadata);
                    const auto relative = source.lexically_relative(layer);
                    index->metadata.insert_or_assign(Text(relative), metadata);
                    const auto [found, inserted] = index->ids.emplace(metadata.id, relative);
                    if (!inserted && found->second != relative)
                        throw std::runtime_error("Duplicate asset ID: " + metadata.id);
                    for (const auto& alias : metadata.previousPaths)
                        index->aliases[alias].insert(relative);
                }
            }
        }
        // 標準シェーダーの旧IDは保持し、ゲーム素材とは異なる明示パスで登録します。
        if (std::filesystem::is_directory(context.ShaderRoot()))
            for (const auto& entry : std::filesystem::recursive_directory_iterator(context.ShaderRoot()))
            {
                if (!entry.is_regular_file() || entry.path().extension() != ".meta")
                    continue;
                auto source = entry.path();
                source.replace_extension();
                if (!std::filesystem::is_regular_file(source))
                    continue;
                const auto rawMetadata = FileContents::Read(entry.path());
                if (!rawMetadata)
                    throw std::runtime_error("Engine shader metadata disappeared: " + Text(entry.path()));
                const auto metadata = ParseMetadata(*rawMetadata);
                const auto relative =
                    std::filesystem::path("Engine/Shaders") / source.lexically_relative(context.ShaderRoot());
                index->metadata.emplace(Text(relative), metadata);
                const auto [found, inserted] = index->ids.emplace(metadata.id, relative);
                if (!inserted && found->second != relative)
                    throw std::runtime_error("Duplicate asset ID: " + metadata.id);
            }
        return index;
    }
}

namespace Engine
{
    auto AssetDatabase::ParseMetadata(std::string_view source) -> AssetMetadata
    {
        const auto json = Json::parse(source);
        AssetMetadata result;
        result.id = json.at("id").get<std::string>();
        result.scale = static_cast<float>(JsonNumber(json.value("scale", Json(1))));
        result.flipV = json.value("flipV", false);
        result.previousPaths = json.value("previousPaths", std::vector<std::string>{});
        if (json.contains("texture"))
        {
            const auto& value = json.at("texture");
            result.texture.mipmaps = value.value("mipmaps", false);
            result.texture.srgb = value.value("srgb", false);
            const double limit = JsonNumber(value.value("maxSize", Json(16384)));
            if (limit < 1 || limit > 16384 || std::floor(limit) != limit)
                throw std::runtime_error("Invalid texture size limit");
            result.texture.maxSize = static_cast<unsigned int>(limit);
            result.texture.compression = value.value("compression", std::string("none"));
        }
        result.texture.Validate();
        if (!ValidId(result.id) || !Engine::InRange(result.scale, 0.0001f, 10000) || result.previousPaths.size() > 1024)
            throw std::runtime_error("Invalid asset metadata");
        if (std::any_of(std::begin(result.previousPaths), std::end(result.previousPaths),
                        [](const auto& previous)
                        {
                            return !Valid(previous);
                        }))
            throw std::runtime_error("Invalid asset alias");
        return result;
    }

    auto AssetDatabase::Read(const std::filesystem::path& path) -> AssetMetadata
    {
        const auto source = FileContents::Read(ContentFiles::Resolve(Sidecar(path)));
        if (!source)
            return {};
        try
        {
            return ParseMetadata(*source);
        }
        catch (const std::exception& error)
        {
            throw std::runtime_error("Invalid asset metadata: " + Text(path) + " / " + error.what());
        }
    }

    auto AssetDatabase::CreateMetadata() -> AssetMetadata
    {
        AssetMetadata metadata;
        std::random_device random;
        constexpr char Hex[] = "0123456789abcdef";
        for (size_t i = 0; i < 32; ++i)
            metadata.id += Hex[random() % 16];
        return metadata;
    }

    auto AssetDatabase::SerializeMetadata(const AssetMetadata& metadata) -> std::string
    {
        metadata.texture.Validate();
        if (!ValidId(metadata.id) || !Engine::InRange(metadata.scale, 0.0001f, 10000))
            throw std::runtime_error("Invalid import settings");
        if (metadata.previousPaths.size() > 1024 ||
            std::any_of(metadata.previousPaths.begin(), metadata.previousPaths.end(),
                        [](const auto& previous)
                        {
                            return !Valid(previous);
                        }))
            throw std::runtime_error("Invalid asset alias history (maximum 1024 paths)");
        return Json{{"version", 1},
                    {"id", metadata.id},
                    {"scale", metadata.scale},
                    {"flipV", metadata.flipV},
                    {"previousPaths", metadata.previousPaths},
                    {"texture",
                     {{"mipmaps", metadata.texture.mipmaps},
                      {"srgb", metadata.texture.srgb},
                      {"maxSize", metadata.texture.maxSize},
                      {"compression", metadata.texture.compression}}}}
                   .dump(2) +
               "\n";
    }

    auto AssetDatabase::Write(const std::filesystem::path& path, const AssetMetadata& metadata,
                              const std::optional<std::string>& expected) -> void
    {
        if (ContentFiles::IsEngineResource(
                std::filesystem::absolute(path).lexically_normal().lexically_relative(ContentFiles::Root(path))))
            throw std::runtime_error("Engine shader resources are read-only");
        const auto source = SerializeMetadata(metadata);
        FileReplacement::Write(Sidecar(path), expected, source);
    }

    auto AssetDatabase::Ensure(const std::filesystem::path& path) -> AssetMetadata
    {
        const auto root = FileSaveLock::Root(path);
        FileSaveLock lock(root);
        if (lock.Outermost())
            FileJournal::RecoverLocked(root);
        auto metadata = Read(path);
        if (!metadata.id.empty())
            return metadata;
        if (!std::filesystem::is_regular_file(path))
            throw std::runtime_error("Asset is missing");
        metadata = CreateMetadata();
        if (ContentFiles::IsEngineResource(
                std::filesystem::absolute(path).lexically_normal().lexically_relative(ContentFiles::Root(path))))
            throw std::runtime_error("Engine shader resources are read-only");
        try
        {
            FileContents::Write(Sidecar(path), SerializeMetadata(metadata), false);
        }
        catch (const std::exception&)
        {
            const auto winner = Read(path);
            if (winner.id.empty())
                throw;
            return winner;
        }
        return metadata;
    }

    auto AssetDatabase::Root(const std::filesystem::path& path) -> std::filesystem::path
    {
        const auto parent = std::filesystem::weakly_canonical(std::filesystem::absolute(path)).parent_path();
        const auto root = FileSaveLock::Root(path);
        if (root != parent || std::filesystem::is_regular_file(root / "WP1.project.json") ||
            std::filesystem::is_regular_file(root / "WP1.runtime.json"))
            return root;
        return {};
    }

    auto AssetDatabase::Resolve(const std::filesystem::path& path, const std::string& id) const -> std::filesystem::path
    {
        if (!Valid(path))
            throw std::runtime_error("Invalid asset reference");
        if (!id.empty())
        {
            const auto found = index_->ids.find(id);
            if (found == index_->ids.end())
                throw std::runtime_error("Missing asset ID: " + id);
            return found->second;
        }
        if (snapshot_ && index_->metadata.contains(Text(path)))
            return path;
        if (std::filesystem::is_regular_file(root_ / path))
            return path;
        const auto found = index_->aliases.find(Text(path));
        if (found == index_->aliases.end())
        {
            if (path.generic_string().starts_with("Shaders/"))
            {
                const auto builtin = std::filesystem::path("Engine") / path;
                if (index_->metadata.contains(Text(builtin)))
                    return builtin;
            }
            return path;
        }
        if (found->second.size() != 1)
            throw std::runtime_error("Ambiguous asset alias: " + Text(path));
        return *found->second.begin();
    }

    auto AssetDatabase::ReferenceId(const std::filesystem::path& path) const -> std::string
    {
        const auto saved = index_->metadata.find(Text(path));
        if (snapshot_)
            return saved == index_->metadata.end() ? std::string{} : saved->second.id;
        const auto raw = FileContents::Read(context_->Resolve(Sidecar(root_ / path)));
        return raw ? ParseMetadata(*raw).id : std::string{};
    }

    auto AssetDatabase::Move(const std::filesystem::path& from, const std::filesystem::path& to) -> void
    {
        if (ContentFiles::IsEngineResource(from) || ContentFiles::IsEngineResource(to))
            throw std::runtime_error("Engine shader resources cannot be moved by a game");
        ValidateDestination(from, to);
        auto metadata = Ensure(root_ / from);
        const auto originalMetadata = metadata;
        if (std::find(metadata.previousPaths.begin(), metadata.previousPaths.end(), Text(from)) ==
            metadata.previousPaths.end())
        {
            if (metadata.previousPaths.size() >= 1024)
                throw std::runtime_error("Asset alias history is full (maximum 1024 paths)");
            metadata.previousPaths.push_back(Text(from));
        }
        std::filesystem::create_directories((root_ / to).parent_path());
        std::string original, revised;
        RewriteDependencies(from, to, original, revised);
        std::filesystem::rename(root_ / from, root_ / to);
        try
        {
            std::filesystem::rename(Sidecar(root_ / from), Sidecar(root_ / to));
            Write(root_ / to, metadata);
            if (!revised.empty())
            {
                std::ofstream output(root_ / to, std::ios::binary | std::ios::trunc);
                output << revised;
                output.close();
                if (!output)
                    throw std::runtime_error("Cannot update model dependencies");
            }
        }
        catch (...)
        {
            std::error_code error;
            if (std::filesystem::exists(Sidecar(root_ / to)))
                std::filesystem::rename(Sidecar(root_ / to), Sidecar(root_ / from), error);
            std::filesystem::rename(root_ / to, root_ / from, error);
            if (!original.empty())
            {
                std::ofstream output(root_ / from, std::ios::binary | std::ios::trunc);
                output << original;
            }
            try
            {
                if (std::filesystem::is_regular_file(root_ / from))
                    Write(root_ / from, originalMetadata);
            }
            catch (...)
            {
            }
            throw;
        }
    }

    auto AssetDatabase::SameCategory(const std::filesystem::path& from, const std::filesystem::path& to) -> bool
    {
        if (*from.begin() != *to.begin() || std::next(from.begin()) == from.end() || std::next(to.begin()) == to.end())
            return false;
        return *from.begin() != "Assets" || *std::next(from.begin()) == *std::next(to.begin());
    }

    auto AssetDatabase::ValidateDestination(const std::filesystem::path& from, const std::filesystem::path& to) const
        -> void
    {
        if (!Valid(from) || !Valid(to) || from.extension() != to.extension() || from == to)
            throw std::runtime_error("Invalid asset destination or destination exists");
        if (!SameCategory(from, to) || std::filesystem::exists(root_ / to) ||
            std::filesystem::exists(Sidecar(root_ / to)))
            throw std::runtime_error("Invalid asset destination or destination exists");
        const auto base = std::filesystem::weakly_canonical(root_);
        for (const auto& path : {from, to})
        {
            const auto relative = std::filesystem::weakly_canonical(root_ / path).lexically_relative(base);
            if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
                throw std::runtime_error("Asset destination leaves project root");
        }
    }

    auto AssetDatabase::RelocateIncludeOperand(std::string& line, size_t directive, const std::filesystem::path& from,
                                               const std::filesystem::path& to) -> void
    {
        const auto begin = line.find_first_not_of(" \t", directive + 7);
        if (begin == std::string::npos || (line[begin] != '\"' && line[begin] != '<'))
            throw std::runtime_error(
                "マクロを使うincludeがあるShaderは、相対参照を保つため同じフォルダー内で改名してください。");
        const auto end = line.find(line[begin] == '\"' ? '\"' : '>', begin + 1);
        if (end == std::string::npos)
            throw std::runtime_error("Shaderのinclude指定が不正です。");
        const auto reference = Path(line.substr(begin + 1, end - begin - 1));
        if (!reference.is_absolute() && !reference.has_root_name())
            line.replace(
                begin + 1, end - begin - 1,
                Text((from.parent_path() / reference).lexically_normal().lexically_relative(to.parent_path())));
    }

    auto AssetDatabase::RelocateShaderLine(std::string& line, const std::filesystem::path& from,
                                           const std::filesystem::path& to) -> void
    {
        auto directive = line.find_first_not_of(" \t");
        if (directive != std::string::npos && line[directive] == '#')
        {
            directive = line.find_first_not_of(" \t", directive + 1);
            if (directive != std::string::npos && line.compare(directive, 7, "include") == 0 &&
                (directive + 7 == line.size() || std::isspace(static_cast<unsigned char>(line[directive + 7])) ||
                 line[directive + 7] == '\"' || line[directive + 7] == '<'))
            {
                RelocateIncludeOperand(line, directive, from, to);
            }
        }
    }

    auto AssetDatabase::RelocateShaderIncludes(const std::string& original, const std::filesystem::path& from,
                                               const std::filesystem::path& to) -> std::string
    {
        std::istringstream lines(original);
        std::string line, revised;
        while (std::getline(lines, line))
        {
            RelocateShaderLine(line, from, to);
            revised += line + '\n';
        }
        return revised;
    }

    auto AssetDatabase::RelocateGltfResources(const std::string& original, const std::filesystem::path& from,
                                              const std::filesystem::path& to) -> std::string
    {
        auto json = Json::parse(original);
        for (const auto* key : {"buffers", "images"})
        {
            if (!json.contains(key))
                continue;
            for (auto& resource : json[key])
            {
                if (!resource.contains("uri"))
                    continue;
                const auto uri = resource["uri"].get<std::string>();
                if (uri.starts_with("data:") || uri.find(":") != std::string::npos)
                    continue;
                resource["uri"] =
                    Text((from.parent_path() / Path(uri)).lexically_normal().lexically_relative(to.parent_path()));
            }
        }
        return json.dump(2) + "\n";
    }

    auto AssetDatabase::RewriteDependencies(const std::filesystem::path& from, const std::filesystem::path& to,
                                            std::string& original, std::string& revised) const -> void
    {
        auto extension = Text(from.extension());
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c)
                       {
                           return static_cast<char>(std::tolower(c));
                       });
        if (from.parent_path() != to.parent_path() && (extension == ".hlsl" || extension == ".hlsli"))
        {
            std::ifstream input(root_ / from, std::ios::binary);
            original = {std::istreambuf_iterator<char>(input), {}};
            if (!input || input.bad())
                throw std::runtime_error("Shaderの移動前データを読み取れません。");
            revised = RelocateShaderIncludes(original, from, to);
            return;
        }
        if (from.parent_path() != to.parent_path() && (extension == ".gltf" || extension == ".obj"))
        {
            std::ifstream input(root_ / from, std::ios::binary);
            original = {std::istreambuf_iterator<char>(input), {}};
            if (extension == ".gltf")
            {
                revised = RelocateGltfResources(original, from, to);
            }
            else
            {
                std::istringstream lines(original);
                std::string line;
                while (std::getline(lines, line))
                {
                    if (line.starts_with("mtllib "))
                        line = "mtllib " + Text((from.parent_path() / Path(line.substr(7)))
                                                    .lexically_normal()
                                                    .lexically_relative(to.parent_path()));
                    revised += line + "\n";
                }
            }
        }
    }
}
