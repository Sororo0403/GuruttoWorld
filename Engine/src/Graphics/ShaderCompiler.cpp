#include <Engine/Graphics/ShaderCompiler.h>
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
        std::ifstream file(path, std::ios::binary);
        if (!file) return false;
        bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        return !file.bad();
    }

    class ShaderIncludes final : public ID3DInclude
    {
    public:
        explicit ShaderIncludes(std::filesystem::path source) : source_(std::move(source)) {}
        HRESULT __stdcall Open(D3D_INCLUDE_TYPE, LPCSTR name, LPCVOID parent, LPCVOID* data, UINT* bytes) override try
        {
            auto base = source_.parent_path();
            if (const auto found = opened_.find(parent); found != opened_.end()) base = found->second.first.parent_path();
            std::error_code error;
            const auto path = std::filesystem::weakly_canonical(base / std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(name))), error);
            if (error) return E_FAIL;
            auto content = std::make_unique<std::string>();
            if (!Read(path, *content) || content->size() > std::numeric_limits<UINT>::max()) return E_FAIL;
            dependencies[path] = *content;
            *bytes = static_cast<UINT>(content->size());
            *data = content->data();
            opened_.emplace(*data, std::make_pair(path, std::move(content)));
            return S_OK;
        }
        catch (...) { return E_FAIL; }
        HRESULT __stdcall Close(LPCVOID data) override
        {
            return opened_.erase(data) == 1 ? S_OK : E_FAIL;
        }
        std::map<std::filesystem::path, std::string> dependencies;
    private:
        std::filesystem::path source_;
        std::map<const void*, std::pair<std::filesystem::path, std::unique_ptr<std::string>>> opened_;
    };
}

namespace Engine
{
    bool GetShaderRevision(const std::filesystem::path& path, std::string& revision) try
    {
        revision.clear();
        std::error_code error;
        const auto sourcePath = std::filesystem::weakly_canonical(path, error);
        if (error) return false;
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
        const auto name = Utf8Path(sourcePath);
        ShaderIncludes includes(sourcePath);
        if (FAILED(D3DPreprocess(bytes.data(), bytes.size(), name.c_str(), nullptr,
            &includes, &expanded, &errors)))
        {
            if (errors) Log::Error(static_cast<const char*>(errors->GetBufferPointer()));
            return false;
        }
        revision.assign(static_cast<const char*>(expanded->GetBufferPointer()), expanded->GetBufferSize());
        includes.dependencies[sourcePath] = std::move(bytes);
        if (cache.size() >= 64) cache.clear();
        cache.emplace(sourcePath, CachedSource{std::move(includes.dependencies), revision});
        return true;
    }

    catch (...) { revision.clear(); return false; }

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
            return true;
        }
        CpuScope scope("Shader compilation");
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        const auto sourceName = Utf8Path(path);
        const HRESULT result = D3DCompile(revision.data(), revision.size(), sourceName.c_str(), nullptr,
            nullptr, entry, target, flags, 0, &shader, &errors);
        if (errors) Log::Error(static_cast<const char*>(errors->GetBufferPointer()));
        if (FAILED(result))
        {
            shader.Reset();
            Log::Error(std::format("{} failed: 0x{:08X}", entry, static_cast<unsigned long>(result)));
            return false;
        }
        // Bytecode does not own a device. Bound the cache when Editor shader revisions accumulate.
        if (cache.size() >= 128) cache.clear();
        cache.emplace(std::move(key), shader);
        return true;
    }
    catch (...) { shader.Reset(); return false; }
}
