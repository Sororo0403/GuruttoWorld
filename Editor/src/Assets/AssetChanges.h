#pragma once
#include "../Project/ProjectCatalog.h"
#include "AssetRevision.h"
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
            AssetRevision revision{};
            bool operator==(const Stamp&) const = default;
        };
        using Files = std::map<std::filesystem::path, Stamp>;
        /// <summary>
        /// シーンの JSON は編集データとして扱い、リソースの再読み込みや未保存の編集内容の置き換えを発生させません。
        /// </summary>
        static Files Capture(const std::filesystem::path& root);
        /// <summary>素材の変更を監視対象へ追加します。</summary>
        void Observe(Files files);
        /// <summary>実行状態と進捗を取得します。</summary>
        void Poll(const std::filesystem::path& root, double seconds);
        /// <summary>反映可能になった変更を取得します。</summary>
        bool TakeReady(bool editing);
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
        /// <summary>シェーダー本体とincludeの内容を比較し、同時刻・同サイズの外部編集も検知します。</summary>
        static std::uint64_t ShaderHash(const std::filesystem::path& path);
        /// <summary>ファイルの拡張子を取得します。</summary>
        static std::string Extension(const std::filesystem::path& path);
        /// <summary>監視対象のパスかを確認します。</summary>
        static bool Watched(const std::filesystem::path& path);
        Files observed_;
        std::future<Files> scan_;
        bool initialized_ = false, pending_ = false, stable_ = false;
        double elapsed_ = 0;
        std::string error_;
    };

    /// <summary>プロジェクト内のシェーダー参照を検証します。</summary>
    bool ValidateProjectShaders(const std::filesystem::path& root, std::string& error);
}
