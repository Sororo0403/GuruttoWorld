#pragma once
#include "../Project/ProjectCatalog.h"
#include <fstream>
#include <cstdint>
#include <stdexcept>

namespace Editor
{
    struct AssetInfo
    {
        std::uintmax_t bytes = 0;
        std::string text, error;
        bool truncated = false;
        /// <summary>内容を読み取ります。</summary>
        static AssetInfo Read(const std::filesystem::path& root, const ProjectAsset& asset);
    };
}
