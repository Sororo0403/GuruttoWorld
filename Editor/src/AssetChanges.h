#pragma once
#include "ProjectCatalog.h"
#include <filesystem>
#include <map>
#include <string>
#include <algorithm>
#include <cmath>
#include <future>
#include <chrono>
#include <array>
#include <cstdint>
#include <fstream>
#include <Engine/Graphics/ShaderCompiler.h>

namespace Editor
{
    class AssetChanges final
    {
    public:
        struct Stamp
        {
            std::filesystem::file_time_type time;
            std::uintmax_t bytes = 0;
            std::uint64_t shaderHash = 0;
            bool operator==(const Stamp&) const = default;
        };
        using Files = std::map<std::filesystem::path, Stamp>;
        /// <summary>
        /// シーンの JSON は編集データとして扱い、リソースの再読み込みや未保存の編集内容の置き換えを発生させません。
        /// </summary>
        static Files Capture(const std::filesystem::path& root)
        {
            if (!std::filesystem::is_directory(root / "Assets"))
                throw std::runtime_error("変更監視対象のAssetsフォルダーがありません。");
            Files files;
            for (const auto& [relative, source] : Engine::ContentFiles::List(root))
                if (Watched(relative))
                    files.emplace(source, Stamp{std::filesystem::last_write_time(source),
                                                std::filesystem::file_size(source), ShaderHash(source)});
            return files;
        }
        void Observe(Files files)
        {
            if (!initialized_)
            {
                observed_ = std::move(files);
                initialized_ = true;
                return;
            }
            if (observed_ != files)
            {
                observed_ = std::move(files);
                pending_ = true;
                stable_ = false;
            }
            else if (pending_)
                stable_ = true;
        }
        void Poll(const std::filesystem::path& root, double seconds)
        {
            // 完了した走査だけをエディターの状態へ反映します。フレーム内でディスク I/O の完了を待ちません。
            if (scan_.valid() && scan_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            {
                try
                {
                    Observe(scan_.get());
                    error_.clear();
                }
                catch (const std::exception& exception)
                {
                    error_ = exception.what();
                }
            }
            if (!std::isfinite(seconds) || seconds <= 0)
                return;
            elapsed_ += std::min(seconds, 1.0);
            if (elapsed_ < 0.5 || scan_.valid())
                return;
            elapsed_ = 0;
            try
            {
                scan_ = std::async(std::launch::async,
                                   [root]
                                   {
                                       return Capture(root);
                                   });
            }
            catch (const std::exception& exception)
            {
                error_ = exception.what();
            }
        }
        bool TakeReady(bool editing)
        {
            if (!editing || !pending_ || !stable_ || !error_.empty())
                return false;
            pending_ = false;
            stable_ = false;
            return true;
        }
        bool Pending() const
        {
            return pending_;
        }
        const std::string& Error() const
        {
            return error_;
        }
        static bool IsShaderSource(const std::filesystem::path& path)
        {
            return Extension(path) == ".hlsl";
        }

    private:
        /// <summary>Shader本体とincludeの内容を比較し、同時刻・同サイズの外部編集も検知します。</summary>
        static std::uint64_t ShaderHash(const std::filesystem::path& path)
        {
            const auto kind = ProjectCatalog::Kind(path);
            if (!kind || *kind != AssetKind::Shader)
                return 0;
            std::ifstream input(path, std::ios::binary);
            if (!input)
                throw std::runtime_error("Shaderの変更監視用データを読み取れません。");
            std::uint64_t hash = 14695981039346656037ULL;
            std::array<char, 4096> buffer{};
            while (input)
            {
                input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                for (std::streamsize i = 0; i < input.gcount(); ++i)
                    hash = (hash ^ static_cast<unsigned char>(buffer[static_cast<size_t>(i)])) * 1099511628211ULL;
            }
            if (!input.eof() || input.bad())
                throw std::runtime_error("Shaderの変更監視用データを読み取れません。");
            return hash;
        }
        static std::string Extension(const std::filesystem::path& path)
        {
            auto value = ProjectCatalog::Text(path.extension());
            std::transform(value.begin(), value.end(), value.begin(),
                           [](unsigned char c)
                           {
                               return static_cast<char>(std::tolower(c));
                           });
            return value;
        }
        static bool Watched(const std::filesystem::path& path)
        {
            const auto kind = ProjectCatalog::Kind(path);
            return (kind && *kind != AssetKind::Scene) || Extension(path) == ".mtl" || Extension(path) == ".meta" ||
                   Extension(path) == ".bin";
        }
        Files observed_;
        std::future<Files> scan_;
        bool initialized_ = false, pending_ = false, stable_ = false;
        double elapsed_ = 0;
        std::string error_;
    };

    inline bool ValidateProjectShaders(const std::filesystem::path& root, std::string& error)
    {
        try
        {
            for (const auto& [relative, source] : Engine::ContentFiles::List(root))
            {
                static_cast<void>(source);
                if (!relative.generic_string().starts_with("Shaders/") || !AssetChanges::IsShaderSource(relative))
                    continue;
                Microsoft::WRL::ComPtr<ID3DBlob> vertex, pixel;
                if (!Engine::CompileShader(root / relative, "VSMain", "vs_5_0", vertex) ||
                    !Engine::CompileShader(root / relative, "PSMain", "ps_5_0", pixel))
                {
                    error = "シェーダーの再読み込みに失敗しました：" + relative.filename().string() +
                            "（コンソールを確認してください）";
                    return false;
                }
            }
            error.clear();
            return true;
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return false;
        }
    }
}
