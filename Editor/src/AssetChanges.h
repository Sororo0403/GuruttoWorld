#pragma once
#include "ProjectCatalog.h"
#include <filesystem>
#include <map>
#include <string>
#include <algorithm>
#include <cmath>
#include <Engine/Graphics/ShaderCompiler.h>

namespace Editor
{
    class AssetChanges final
    {
    public:
        struct Stamp
        {
            std::filesystem::file_time_type time;
            std::uintmax_t bytes=0;
            bool operator==(const Stamp&) const = default;
        };
        using Files=std::map<std::filesystem::path,Stamp>;
        // Scene JSON is authoring data; it never triggers a resource reload or replaces unsaved edits.
        static Files Capture(const std::filesystem::path& root)
        {
            Files files;
            for (const auto& directory : {root/"Assets",root/"Shaders"})
                for (const auto& entry : std::filesystem::recursive_directory_iterator(directory))
                    if (entry.is_regular_file() && Watched(entry.path().lexically_relative(root)))
                        files.emplace(entry.path(),Stamp{entry.last_write_time(),entry.file_size()});
            return files;
        }
        void Observe(Files files)
        {
            if (!initialized_) { observed_=std::move(files); initialized_=true; return; }
            if (observed_!=files) { observed_=std::move(files); pending_=true; stable_=false; }
            else if (pending_) stable_=true;
        }
        void Poll(const std::filesystem::path& root, double seconds)
        {
            if (!std::isfinite(seconds) || seconds<=0) return;
            elapsed_+=std::min(seconds,1.0);
            if (elapsed_<0.5) return;
            elapsed_=0;
            try { Observe(Capture(root)); error_.clear(); }
            catch (const std::exception& exception) { error_=exception.what(); }
        }
        bool TakeReady(bool editing)
        {
            if (!editing || !pending_ || !stable_ || !error_.empty()) return false;
            pending_=false; stable_=false;
            return true;
        }
        bool Pending() const { return pending_; }
        const std::string& Error() const { return error_; }
        static bool IsShaderSource(const std::filesystem::path& path) { return Extension(path)==".hlsl"; }
    private:
        static std::string Extension(const std::filesystem::path& path)
        {
            auto value=ProjectCatalog::Text(path.extension());
            std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return value;
        }
        static bool Watched(const std::filesystem::path& path)
        {
            const auto kind=ProjectCatalog::Kind(path);
            return (kind && *kind!=AssetKind::Scene) || Extension(path)==".mtl";
        }
        Files observed_;
        bool initialized_=false, pending_=false, stable_=false;
        double elapsed_=0;
        std::string error_;
    };

    inline bool ValidateProjectShaders(const std::filesystem::path& root, std::string& error)
    {
        try
        {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(root/"Shaders"))
            {
                if (!entry.is_regular_file() || !AssetChanges::IsShaderSource(entry.path())) continue;
                Microsoft::WRL::ComPtr<ID3DBlob> vertex, pixel;
                if (!Engine::CompileShader(entry.path(),"VSMain","vs_5_0",vertex) ||
                    !Engine::CompileShader(entry.path(),"PSMain","ps_5_0",pixel))
                { error="シェーダーの再読み込みに失敗しました："+entry.path().filename().string()+"（コンソールを確認してください）"; return false; }
            }
            error.clear();
            return true;
        }
        catch (const std::exception& exception) { error=exception.what(); return false; }
    }
}
