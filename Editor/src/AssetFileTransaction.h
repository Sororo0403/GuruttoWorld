#pragma once
#include <Engine/Platform/Window.h>
#include <filesystem>
#include <fstream>
#include <optional>
#include <functional>
#include <chrono>

namespace Editor
{
    // Restore both the asset and its ID metadata when scene preparation fails.
    class AssetFileTransaction final
    {
    public:
        static bool Run(const std::filesystem::path& path,std::string& error,const std::function<bool()>& apply)
        {
            auto metadata=path; metadata+=".meta";
            std::optional<std::string> asset,meta;
            try { asset=Read(path); meta=Read(metadata); }
            catch (const std::exception& failure) { error=failure.what(); return false; }
            try { if (apply()) return true; }
            catch (const std::exception& failure) { error=failure.what(); }
            try { Restore(path,asset); Restore(metadata,meta); }
            catch (const std::exception& failure) { error+=" / ロールバックに失敗しました："+std::string(failure.what()); }
            return false;
        }
    private:
        static std::optional<std::string> Read(const std::filesystem::path& path)
        {
            if (!std::filesystem::exists(path)) return std::nullopt;
            std::ifstream input(path,std::ios::binary);
            if (!input) throw std::runtime_error("アセットの復元用データを読み取れません。");
            std::string source{std::istreambuf_iterator<char>(input),{}};
            if (input.bad()) throw std::runtime_error("アセットの復元用データを読み取れません。");
            return source;
        }
        static void Restore(const std::filesystem::path& path,const std::optional<std::string>& source)
        {
            if (!source) { std::filesystem::remove(path); return; }
            if (Read(path)==source) return;
            auto temporary=path; temporary+=".rollback-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
            std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
            output.write(source->data(),static_cast<std::streamsize>(source->size())); output.close();
            if (!output || !MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            { std::error_code ignored; std::filesystem::remove(temporary,ignored); throw std::runtime_error("アセットを復元できません。"); }
        }
    };
}
