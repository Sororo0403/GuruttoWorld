#include <Engine/Assets/AssetArtifact.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <future>
#include <map>
#include <mutex>
#include <stdexcept>
#include <Windows.h>
#include <bcrypt.h>
#include <objbase.h>

#pragma comment(lib, "Bcrypt.lib")
#pragma comment(lib, "Ole32.lib")

namespace
{
    constexpr std::size_t MaximumBytes = 256 * 1024 * 1024;
    bool ValidKey(std::string_view key)
    {
        return key.size() == 64 && std::all_of(key.begin(), key.end(),
                                               [](char c)
                                               {
                                                   return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                                               });
    }
    void Append(std::string& output, std::string_view value)
    {
        output += std::to_string(value.size());
        output += ':';
        output += value;
    }
    class Hash final
    {
        BCRYPT_ALG_HANDLE algorithm_{};
        BCRYPT_HASH_HANDLE hash_{};

    public:
        Hash()
        {
            if (BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
                throw std::runtime_error("Cannot open artifact hash provider");
            if (BCryptCreateHash(algorithm_, &hash_, nullptr, 0, nullptr, 0, 0) < 0)
            {
                BCryptCloseAlgorithmProvider(algorithm_, 0);
                throw std::runtime_error("Cannot create artifact hash");
            }
        }
        ~Hash()
        {
            BCryptDestroyHash(hash_);
            BCryptCloseAlgorithmProvider(algorithm_, 0);
        }
        void Add(std::string_view bytes)
        {
            while (!bytes.empty())
            {
                const auto count = static_cast<ULONG>(std::min<std::size_t>(bytes.size(), 65536));
                if (BCryptHashData(hash_, reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())), count, 0) < 0)
                    throw std::runtime_error("Cannot hash artifact input");
                bytes.remove_prefix(count);
            }
        }
        std::string Finish()
        {
            std::array<unsigned char, 32> bytes{};
            if (BCryptFinishHash(hash_, bytes.data(), static_cast<ULONG>(bytes.size()), 0) < 0)
                throw std::runtime_error("Cannot finish artifact hash");
            constexpr char Hex[] = "0123456789abcdef";
            std::string result;
            for (const auto byte : bytes)
            {
                result += Hex[byte >> 4];
                result += Hex[byte & 15];
            }
            return result;
        }
    };
    std::optional<std::string> Revision(const std::filesystem::path& path)
    {
        if (!std::filesystem::exists(path))
            return {};
        return Engine::ArtifactHash::File(path);
    }
}
namespace Engine
{
    std::string ArtifactDescriptor::Canonical() const
    {
        if (kind.empty() || assetId.empty() || sourceRevision.empty() || importerVersion.empty() || platform.empty())
            throw std::invalid_argument("Incomplete artifact descriptor");
        std::string result = "WP1ART02";
        for (const auto* value : {&kind, &assetId, &sourceRevision, &importerVersion, &settings, &platform, &compiler})
            Append(result, *value);
        auto ordered = dependencies;
        std::sort(ordered.begin(), ordered.end(),
                  [](const auto& a, const auto& b)
                  {
                      return a.id < b.id;
                  });
        std::string previous;
        bool first = true;
        for (const auto& dependency : ordered)
        {
            if (dependency.id.empty() || dependency.revision.empty() || (!first && dependency.id == previous))
                throw std::invalid_argument("Invalid or duplicate artifact dependency");
            Append(result, dependency.id);
            Append(result, dependency.revision);
            previous = dependency.id;
            first = false;
        }
        return result;
    }
    std::string ArtifactDescriptor::Key() const
    {
        return ArtifactHash::Bytes(Canonical());
    }
    std::string ArtifactHash::Bytes(std::string_view bytes)
    {
        Hash hash;
        hash.Add(bytes);
        return hash.Finish();
    }
    std::string ArtifactHash::File(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            throw std::runtime_error("Cannot read artifact source");
        Hash hash;
        std::array<char, 65536> buffer{};
        while (file)
        {
            file.read(buffer.data(), buffer.size());
            hash.Add({buffer.data(), static_cast<std::size_t>(file.gcount())});
        }
        if (!file.eof() || file.bad())
            throw std::runtime_error("Cannot finish reading artifact source");
        return hash.Finish();
    }
    ArtifactSources ArtifactSources::Capture(const std::vector<std::pair<std::string, std::filesystem::path>>& files)
    {
        ArtifactSources result;
        for (const auto& [id, path] : files)
            result.files_.push_back({id, path, Revision(path)});
        return result;
    }
    void ArtifactSources::Describe(ArtifactDescriptor& descriptor) const
    {
        for (const auto& source : files_)
            descriptor.dependencies.push_back({source.id, source.revision ? "sha256:" + *source.revision : "missing"});
    }
    bool ArtifactSources::Unchanged() const noexcept
    {
        try
        {
            for (const auto& source : files_)
                if (Revision(source.path) != source.revision)
                    return false;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
    ArtifactStore::ArtifactStore(std::filesystem::path root) : root_(std::move(root)) {}
    std::shared_ptr<const ArtifactData> ArtifactStore::Read(const std::string& key) const noexcept
    {
        try
        {
            if (!ValidKey(key))
                return {};
            const auto path = root_ / (key + ".artifact");
            const auto size = std::filesystem::file_size(path);
            if (size < 136 || size > MaximumBytes + 136)
                return {};
            std::ifstream input(path, std::ios::binary);
            std::string contents(static_cast<std::size_t>(size), '\0');
            if (!input.read(contents.data(), contents.size()) || input.peek() != std::char_traits<char>::eof() ||
                contents.substr(0, 8) != "WP1ART02" || contents.substr(8, 64) != key)
                return {};
            auto result = std::make_shared<ArtifactData>();
            result->key = key;
            result->bytes = contents.substr(136);
            if (ArtifactHash::Bytes(result->bytes) != contents.substr(72, 64))
                return {};
            return result;
        }
        catch (...)
        {
            return {};
        }
    }
    bool ArtifactStore::Write(const ArtifactData& data) const noexcept
    {
        std::filesystem::path temporary;
        try
        {
            if (!ValidKey(data.key) || data.bytes.size() > MaximumBytes)
                return false;
            std::filesystem::create_directories(root_);
            GUID guid{};
            if (FAILED(CoCreateGuid(&guid)))
                return false;
            std::array<wchar_t, 40> identity{};
            if (!StringFromGUID2(guid, identity.data(), static_cast<int>(identity.size())))
                return false;
            temporary = root_ / (data.key + "." + std::filesystem::path(identity.data()).string() + ".tmp");
            {
                std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
                output << "WP1ART02" << data.key << ArtifactHash::Bytes(data.bytes);
                output.write(data.bytes.data(), data.bytes.size());
                output.close();
                if (!output)
                    throw std::runtime_error("Cannot write artifact cache");
            }
            if (!MoveFileExW(temporary.c_str(), (root_ / (data.key + ".artifact")).c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("Cannot publish artifact cache");
            return true;
        }
        catch (...)
        {
            std::error_code ignored;
            if (!temporary.empty())
                std::filesystem::remove(temporary, ignored);
            return false;
        }
    }
    struct ArtifactTicket::State
    {
        std::string assetId, key;
        ArtifactSources sources;
        std::shared_future<std::shared_ptr<const ArtifactData>> future;
        std::mutex mutex;
        std::string error;
    };
    struct ArtifactRequests::Impl
    {
        std::shared_ptr<const ArtifactStore> store;
        std::map<std::string, std::shared_ptr<ArtifactTicket::State>> current;
        std::map<std::string, std::shared_ptr<const ArtifactData>> published;
        std::vector<std::shared_future<void>> jobs;
        mutable std::mutex mutex;
    };
    bool ArtifactTicket::Ready() const
    {
        return state_ && state_->future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }
    std::shared_ptr<const ArtifactData> ArtifactTicket::Poll() const
    {
        return Ready() ? state_->future.get() : nullptr;
    }
    std::string ArtifactTicket::Error() const
    {
        if (!state_)
            return "Missing artifact request";
        std::lock_guard lock(state_->mutex);
        return state_->error;
    }
    ArtifactRequests::ArtifactRequests(std::shared_ptr<const ArtifactStore> store) : impl_(std::make_unique<Impl>())
    {
        impl_->store = std::move(store);
    }
    ArtifactRequests::~ArtifactRequests() = default;
    ArtifactTicket ArtifactRequests::Request(ArtifactDescriptor descriptor, ArtifactSources sources, Build build)
    {
        sources.Describe(descriptor);
        const auto key = descriptor.Key();
        std::lock_guard lock(impl_->mutex);
        ArtifactTicket ticket;
        if (const auto found = impl_->current.find(descriptor.assetId);
            found != impl_->current.end() && found->second->key == key)
        {
            ticket.state_ = found->second;
            return ticket;
        }
        auto state = std::make_shared<ArtifactTicket::State>();
        state->assetId = descriptor.assetId;
        state->key = key;
        state->sources = std::move(sources);
        auto promise = std::make_shared<std::promise<std::shared_ptr<const ArtifactData>>>();
        state->future = promise->get_future().share();
        std::erase_if(impl_->jobs,
                      [](const auto& job)
                      {
                          return job.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
                      });
        impl_->jobs.push_back(std::async(std::launch::async,
                                         [state, promise, store = impl_->store, build = std::move(build)]
                                         {
                                             try
                                             {
                                                 if (!state->sources.Unchanged())
                                                     throw std::runtime_error("Artifact sources changed before import");
                                                 auto data = store ? store->Read(state->key) : nullptr;
                                                 if (!data)
                                                     data = std::make_shared<const ArtifactData>(
                                                         ArtifactData{state->key, build()});
                                                 if (!state->sources.Unchanged())
                                                     throw std::runtime_error("Artifact sources changed during import");
                                                 if (store)
                                                     static_cast<void>(store->Write(*data));
                                                 promise->set_value(std::move(data));
                                             }
                                             catch (...)
                                             {
                                                 {
                                                     std::lock_guard errorLock(state->mutex);
                                                     try
                                                     {
                                                         throw;
                                                     }
                                                     catch (const std::exception& failure)
                                                     {
                                                         state->error = failure.what();
                                                     }
                                                     catch (...)
                                                     {
                                                         state->error = "Artifact import failed";
                                                     }
                                                 }
                                                 promise->set_value(nullptr);
                                             }
                                         })
                                  .share());
        impl_->current.insert_or_assign(state->assetId, state);
        ticket.state_ = std::move(state);
        return ticket;
    }
    bool ArtifactRequests::Publish(const ArtifactTicket& ticket)
    {
        if (!ticket.state_)
            return false;
        const auto data = ticket.Poll();
        if (!data || !ticket.state_->sources.Unchanged())
            return false;
        std::lock_guard lock(impl_->mutex);
        const auto found = impl_->current.find(ticket.state_->assetId);
        if (found == impl_->current.end() || found->second != ticket.state_)
            return false;
        impl_->published.insert_or_assign(ticket.state_->assetId, data);
        return true;
    }
    std::shared_ptr<const ArtifactData> ArtifactRequests::Published(const std::string& assetId) const
    {
        std::lock_guard lock(impl_->mutex);
        const auto found = impl_->published.find(assetId);
        return found == impl_->published.end() ? nullptr : found->second;
    }
    void ArtifactRequests::Cancel(const std::string& assetId)
    {
        std::lock_guard lock(impl_->mutex);
        impl_->current.erase(assetId);
    }
}
