#include <Engine/Graphics/ShaderCompiler.h>
#include <Engine/Assets/AssetDatabase.h>
#include <Engine/Core/Log.h>
#include <Engine/Core/Profiler.h>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <tuple>
#include <limits>
#include <memory>
#include <bcrypt.h>
#include <array>
#include <vector>
#include <cstdlib>
#include <regex>
#include <optional>

#pragma comment(lib, "Bcrypt.lib")

#pragma comment(lib, "D3DCompiler.lib")

namespace
{
    std::string Utf8Path(const std::filesystem::path& path)
    {
        const auto bytes = path.u8string();
        return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    }

    bool Read(const std::filesystem::path& path, std::string& bytes)
    {
        std::ifstream file(Engine::ContentFiles::Resolve(path), std::ios::binary);
        if (!file) return false;
        bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        return !file.bad();
    }

    std::string Digest(const std::string& bytes)
    {
        BCRYPT_ALG_HANDLE algorithm{};
        BCRYPT_HASH_HANDLE hash{};
        std::array<unsigned char, 32> result{};
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return {};
        const auto created = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
        const auto hashed = created < 0 ? created : BCryptHashData(hash,
            reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())), static_cast<ULONG>(bytes.size()), 0);
        const auto finished = hashed < 0 ? hashed : BCryptFinishHash(hash, result.data(), static_cast<ULONG>(result.size()), 0);
        if (hash) BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        if (finished < 0) return {};
        constexpr char hex[] = "0123456789abcdef";
        std::string text;
        for (const auto value : result) { text += hex[value >> 4]; text += hex[value & 15]; }
        return text;
    }

    std::string CompilerIdentity()
    {
        static const std::string identity = []
        {
            // D3DCompile is declared without dllimport; taking its address can identify the host import thunk.
            const auto module = GetModuleHandleW(D3DCOMPILER_DLL_W);
            if (!module || !GetProcAddress(module, "D3DCompile")) return std::string{};
            std::vector<wchar_t> path(32768);
            const auto length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
            std::string bytes;
            return length && length < path.size() && Read(std::filesystem::path(path.data()), bytes) ? Digest(bytes) : std::string{};
        }();
        return identity;
    }

    std::filesystem::path LocalCache()
    {
        wchar_t* value{}; size_t count{};
        if (_wdupenv_s(&value, &count, L"LOCALAPPDATA") != 0 || !value) return {};
        std::filesystem::path path(value); free(value);
        return path / "WP1/shader-cache/v1";
    }

    std::filesystem::path PackagedCache(std::filesystem::path path)
    {
        for (path = std::filesystem::absolute(path).parent_path(); !path.empty();)
        {
            if (path.filename() == "Shaders") return path / "Compiled";
            const auto parent = path.parent_path();
            if (parent == path) break;
            path = parent;
        }
        return {};
    }

    bool LoadBytecode(const std::filesystem::path& directory, const std::string& key,
        Microsoft::WRL::ComPtr<ID3DBlob>& shader)
    {
        if (directory.empty() || key.empty()) return false;
        std::error_code error;
        const auto file = directory / (key + ".bin");
        const auto size = std::filesystem::file_size(file, error);
        if (error || size < 140 || size > 16 * 1024 * 1024) return false;
        std::string bytes;
        if (!Read(file, bytes) || bytes.substr(0, 8) != "WP1SHD01" || bytes.substr(8, 64) != key) return false;
        const auto payload = bytes.substr(136);
        if (bytes.substr(72, 64) != Digest(payload) || payload.substr(0, 4) != "DXBC") return false;
        if (FAILED(D3DCreateBlob(payload.size(), &shader))) return false;
        memcpy(shader->GetBufferPointer(), payload.data(), payload.size());
        return true;
    }

    bool SaveBytecode(const std::filesystem::path& directory, const std::string& key, ID3DBlob* shader) try
    {
        if (directory.empty() || key.empty() || !shader) return false;
        const std::string bytes(static_cast<const char*>(shader->GetBufferPointer()), shader->GetBufferSize());
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error) return false;
        const auto temporary = directory / (key + "-" + std::to_string(GetCurrentProcessId()) + ".tmp");
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file << "WP1SHD01" << key << Digest(bytes);
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        file.close();
        if (!file) return false;
        const auto destination = directory / (key + ".bin");
        if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        { std::filesystem::remove(temporary, error); return false; }
        return true;
    }
    catch (...) { return false; }

    thread_local bool exportingCache = false;

    std::string PortableRevision(const std::string& expanded)
    {
        std::string portable;
        portable.reserve(expanded.size());
        for (size_t begin = 0; begin < expanded.size();)
        {
            const auto newline = expanded.find('\n', begin);
            const auto end = newline == std::string::npos ? expanded.size() : newline + 1;
            auto line = expanded.substr(begin, end - begin);
            if (line.starts_with("#line "))
            {
                const auto first = line.find('"');
                const auto last = line.rfind('"');
                if (first != std::string::npos && last > first)
                {
                    const auto file = line.substr(first + 1, last - first - 1);
                    const auto separator = file.find_last_of("\\/");
                    if (separator != std::string::npos) line.replace(first + 1, last - first - 1, file.substr(separator + 1));
                }
            }
            // Only line metadata is normalized. Expanded __FILE__ or other macro values retain their semantics.
            portable += line;
            begin = end;
        }
        return portable;
    }

    class ShaderIncludes final : public ID3DInclude
    {
    public:
        explicit ShaderIncludes(std::filesystem::path source) : source_(std::move(source)) {}
        HRESULT __stdcall Open(D3D_INCLUDE_TYPE, LPCSTR name, LPCVOID parent, LPCVOID* data, UINT* bytes) override
        {
            try {
            auto base = source_.parent_path();
            if (const auto found = opened_.find(parent); found != opened_.end()) base = found->second.first.parent_path();
            std::error_code error;
            auto path = std::filesystem::weakly_canonical(base / std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(name))), error);
            if (error) return E_FAIL;
            auto content = std::make_unique<std::string>();
            if (!Read(path, *content))
            {
                const auto root=Engine::AssetDatabase::Root(source_);
                const auto relative=path.lexically_relative(root);
                if(root.empty() || !Engine::AssetDatabase::Valid(relative)) return E_FAIL;
                if(!assets_) assets_.emplace(root);
                path=std::filesystem::weakly_canonical(root/assets_->Resolve(relative),error);
                if(error || !Read(path,*content)) return E_FAIL;
                usedAlias=true;
            }
            if (content->size() > std::numeric_limits<UINT>::max()) return E_FAIL;
            dependencies[path] = *content;
            const auto address=content->data();
            const auto [stored,inserted]=opened_.emplace(address,std::make_pair(path,std::move(content)));
            if(!inserted) return E_FAIL;
            *bytes=static_cast<UINT>(stored->second.second->size());
            *data=stored->second.second->data();
            return S_OK;
        }
        catch (...) { return E_FAIL; }
        }
        HRESULT __stdcall Close(LPCVOID data) override
        {
            return opened_.erase(data) == 1 ? S_OK : E_FAIL;
        }
        std::map<std::filesystem::path, std::string> dependencies;
        bool usedAlias=false;
    private:
        std::filesystem::path source_;
        std::optional<Engine::AssetDatabase> assets_;
        std::map<const void*, std::pair<std::filesystem::path, std::unique_ptr<std::string>>> opened_;
    };
}

namespace Engine
{
    bool GetShaderRevision(const std::filesystem::path& path, std::string& revision) try
    {
        revision.clear();
        std::error_code error;
        auto sourcePath = std::filesystem::weakly_canonical(path, error);
        if (error) return false;
        if(!std::filesystem::is_regular_file(ContentFiles::Resolve(sourcePath)))
        {
            const auto root=AssetDatabase::Root(sourcePath);
            const auto relative=sourcePath.lexically_relative(root);
            if(root.empty() || !AssetDatabase::Valid(relative)) return false;
            sourcePath=std::filesystem::weakly_canonical(root/AssetDatabase(root).Resolve(relative),error);
            if(error || !std::filesystem::is_regular_file(ContentFiles::Resolve(sourcePath))) return false;
        }
        struct CachedSource
        {
            std::map<std::filesystem::path, std::string> dependencies;
            std::string revision;
        };
        static std::mutex mutex;
        static std::map<std::filesystem::path, CachedSource> cache;
        std::lock_guard lock(mutex);
        if (const auto found = cache.find(sourcePath); found != cache.end())
        {
            bool unchanged = true;
            for (const auto& [dependency, previous] : found->second.dependencies)
            {
                std::string current;
                if (!Read(dependency, current) || current != previous) { unchanged = false; break; }
            }
            if (unchanged) { revision = found->second.revision; return true; }
            cache.erase(found);
        }
        std::string bytes;
        if (!Read(sourcePath, bytes)) return false;
        Microsoft::WRL::ComPtr<ID3DBlob> expanded, errors;
        const auto name = Utf8Path(sourcePath.filename());
        ShaderIncludes includes(sourcePath);
        if (FAILED(D3DPreprocess(bytes.data(), bytes.size(), name.c_str(), nullptr,
            &includes, &expanded, &errors)))
        {
            if (errors) Log::Error(static_cast<const char*>(errors->GetBufferPointer()));
            return false;
        }
        revision.assign(static_cast<const char*>(expanded->GetBufferPointer()), expanded->GetBufferSize());
        // D3DPreprocess expands even a filename-only source name against the process working directory.
        revision = PortableRevision(revision);
        includes.dependencies[sourcePath] = std::move(bytes);
        if (cache.size() >= 64) cache.clear();
        // Recheck aliases on every preprocess so metadata changes and recreated old paths cannot leave a stale include.
        if(!includes.usedAlias) cache.emplace(sourcePath, CachedSource{std::move(includes.dependencies), revision});
        return true;
    }

    catch (...) { revision.clear(); return false; }

    static bool CompileBytecode(const std::filesystem::path &path, const std::string &revision, const char *entry, const char *target, UINT flags, Microsoft::WRL::ComPtr<ID3DBlob> &shader)
    {
        CpuScope scope("Shader compilation");
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        const auto sourceName = Utf8Path(path);
        const HRESULT result = D3DCompile(revision.data(), revision.size(), sourceName.c_str(), nullptr,
                                          nullptr, entry, target, flags, 0, &shader, &errors);
        if (errors) Log::Error(static_cast<const char *>(errors->GetBufferPointer()));
        if (FAILED(result))
        {
            shader.Reset();
            Log::Error(std::format("{} failed: 0x{:08X}", entry, static_cast<unsigned long>(result)));
            return false;
        }
        return true;
    }
    static bool ExportCachedShader(const std::filesystem::path &path, const std::string &revision, const char *entry, const char *target, UINT flags, ID3DBlob *shader)
    {
        if (exportingCache)
        {
            const auto compiler = CompilerIdentity();
            const auto persistentKey = compiler.empty() ? std::string{} : Digest(
                    "WP1SHD01\n" + compiler + "\n" + entry + "\n" + target + "\n" + std::to_string(flags) + "\n" + revision);
            if (!SaveBytecode(PackagedCache(path), persistentKey, shader)) return false;
        }
        return true;
    }
    template <class Cache, class Key> static void RememberShader(Cache &cache, Key key, const Microsoft::WRL::ComPtr<ID3DBlob> &shader) {
        if (cache.size() >= 128) cache.clear();
        cache.emplace(std::move(key), shader);
    }
    bool CompileShader(const std::filesystem::path& path, const char* entry, const char* target,
        Microsoft::WRL::ComPtr<ID3DBlob>& shader) try
    {
        shader.Reset();
        if (!entry || !target) return false;
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS;
#if defined(_DEBUG)
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
        std::string revision;
        if (!GetShaderRevision(path, revision)) return false;
        using Key = std::tuple<std::string, std::string, std::string, UINT>;
        static std::mutex mutex;
        static std::map<Key, Microsoft::WRL::ComPtr<ID3DBlob>> cache;
        std::lock_guard lock(mutex);
        Key key{revision, entry, target, flags};
        if (const auto found = cache.find(key); found != cache.end())
        {
            shader = found->second;
            if(!ExportCachedShader(path,revision,entry,target,flags,shader.Get())) return false;
            return true;
        }
        const auto compiler = CompilerIdentity();
        const auto persistentKey = compiler.empty() ? std::string{} : Digest(
            "WP1SHD01\n" + compiler + "\n" + entry + "\n" + target + "\n" + std::to_string(flags) + "\n" + revision);
        const auto packaged = PackagedCache(path);
        const auto local = LocalCache();
        bool loaded{};
        {
            CpuScope lookupScope("Shader bytecode lookup");
            loaded = LoadBytecode(packaged, persistentKey, shader) || LoadBytecode(local, persistentKey, shader);
        }
        if (loaded)
        {
            if (exportingCache && !SaveBytecode(packaged, persistentKey, shader.Get())) return false;
            RememberShader(cache,std::move(key),shader);
            return true;
        }
        if(!CompileBytecode(path,revision,entry,target,flags,shader)) return false;
        if (exportingCache)
        {
            if (!SaveBytecode(packaged, persistentKey, shader.Get())) return false;
        }
        else SaveBytecode(local, persistentKey, shader.Get());
        // Bytecode does not own a device. Bound the cache when Editor shader revisions accumulate.
        RememberShader(cache,std::move(key),shader);
        return true;
    }
    catch (...) { shader.Reset(); return false; }

    bool PrepareShaderCache(const std::filesystem::path& shaders, std::string& error) try
    {
        struct Restore { ~Restore() { exportingCache = false; } } restore;
        exportingCache = true;
        for (const auto& file : std::filesystem::directory_iterator(shaders))
        {
            if (file.path().extension() != ".hlsl") continue;
            std::string source;
            if (!GetShaderRevision(file.path(), source)) { error = "Cannot preprocess shader source"; return false; }
            for (const auto& [entry, target] : {std::pair{"VSMain", "vs_5_0"}, {"PSMain", "ps_5_0"},
                {"VSShadow", "vs_5_0"}, {"VSBounds", "vs_5_0"}})
            {
                if (!std::regex_search(source, std::regex(std::string("\\b") + entry + "\\s*\\("))) continue;
                Microsoft::WRL::ComPtr<ID3DBlob> shader;
                if (!CompileShader(file.path(), entry, target, shader))
                { error = "Shader cache preparation failed: " + Utf8Path(file.path()) + " / " + entry; return false; }
            }
        }
        return true;
    }
    catch (const std::exception& exception) { error = exception.what(); return false; }


}
