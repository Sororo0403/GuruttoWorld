#pragma once
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
                    if (entry.is_regular_file() && entry.path().extension()!=L".json")
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
    private:
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
                if (!entry.is_regular_file() || entry.path().extension()!=L".hlsl") continue;
                Microsoft::WRL::ComPtr<ID3DBlob> vertex, pixel;
                if (!Engine::CompileShader(entry.path(),"VSMain","vs_5_0",vertex) ||
                    !Engine::CompileShader(entry.path(),"PSMain","ps_5_0",pixel))
                { error="Shader reload failed: "+entry.path().filename().string()+" (see Console)"; return false; }
            }
            error.clear();
            return true;
        }
        catch (const std::exception& exception) { error=exception.what(); return false; }
    }
}
