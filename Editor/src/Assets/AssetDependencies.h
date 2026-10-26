#pragma once
#include "../Project/ProjectCatalog.h"
#include "AssetRevision.h"
#include <Engine/Core/Json.h>
#include <fstream>
#include <sstream>
#include <chrono>

namespace Editor
{
    class AssetDependencies final
    {
    public:
        /// <summary>現在の素材から依存情報を更新します。</summary>
        void Refresh(const std::filesystem::path& root, const ProjectAsset& asset,
                     std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());
        const std::vector<std::string>& Values() const
        {
            return values_;
        }
        const std::string& Error() const
        {
            return error_;
        }
        size_t Reads() const
        {
            return reads_;
        }

    private:
        /// <summary>素材が参照するパスを収集します。</summary>
        static void CollectReferences(const Engine::Json& value, std::set<std::string>& values);
        /// <summary>素材の依存先を読み取ります。</summary>
        static std::set<std::string> ReadDependencies(std::istream& input, bool json, const std::string& extension);

        std::filesystem::path path_;
        AssetRevision revision_{};
        std::chrono::steady_clock::time_point next_{};
        std::vector<std::string> values_;
        std::string error_;
        size_t reads_ = 0;
        bool loaded_ = false;
    };
}
