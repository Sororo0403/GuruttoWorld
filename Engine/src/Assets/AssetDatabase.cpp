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
        for (const auto& layer : context.Layers())
        {
            notifications.push_back(std::make_unique<ChangeNotification>(layer, false, FILE_NOTIFY_CHANGE_DIR_NAME));
            for (const auto* folder : {"Assets", "Shaders"})
                if (std::filesystem::is_directory(layer / folder))
                    notifications.push_back(std::make_unique<ChangeNotification>(layer / folder, true, Contents));
        }
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
        const auto& layers = context->Layers();
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
        if (entry.Changed())
        {
            // 走査前に通知を登録し、走査中の変更は次回の取得時にも必ず反映します。
            entry.ready = false;
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
        std::map<std::filesystem::path, std::string> identities;
        for (const auto& layer : context.Layers())
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
                    const auto identity = identities.find(relative);
                    if (identity != identities.end() && identity->second != metadata.id)
                        throw std::runtime_error("Shared override must preserve asset ID: " + Text(relative));
                    identities[relative] = metadata.id;
                    index->metadata.insert_or_assign(Text(relative), metadata);
                    const auto [found, inserted] = index->ids.emplace(metadata.id, relative);
                    if (!inserted && found->second != relative)
                        throw std::runtime_error("Duplicate asset ID: " + metadata.id);
                    for (const auto& alias : metadata.previousPaths)
                        index->aliases[alias].insert(relative);
                }
            }
        }
        return index;
    }
}
