#include <Engine/Assets/AssetSourceStamp.h>

namespace Engine
{
    auto AssetSourceStamp::Capture(const std::filesystem::path& path) -> std::optional<AssetSourceStamp>
    try
    {
        if (path.empty())
            return AssetSourceStamp{};
        const auto contents = Hash(path);
        if (!contents)
            return {};
        auto sidecar = path;
        sidecar += ".meta";
        const auto resolved = ContentFiles::Resolve(sidecar);
        std::error_code error;
        const bool exists = std::filesystem::exists(resolved, error);
        if (error)
            return {};
        std::optional<Digest> metadata;
        if (exists)
        {
            metadata = Hash(resolved);
            if (!metadata)
                return {};
        }
        return AssetSourceStamp{*contents, metadata};
    }
    catch (const std::exception&)
    {
        return {};
    }

    auto AssetSourceStamp::Hash(const std::filesystem::path& path) -> std::optional<Digest>
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            return {};
        BCRYPT_ALG_HANDLE algorithm{};
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
            return {};
        BCRYPT_HASH_HANDLE hash{};
        if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0)
        {
            BCryptCloseAlgorithmProvider(algorithm, 0);
            return {};
        }
        std::array<char, 65536> buffer{};
        bool valid = true;
        while (file)
        {
            file.read(buffer.data(), buffer.size());
            if (BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(file.gcount()), 0) < 0)
            {
                valid = false;
                break;
            }
        }
        Digest digest{};
        valid = valid && file.eof() && !file.bad() &&
                BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0;
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return valid ? std::optional<Digest>{digest} : std::nullopt;
    }
}
