#pragma once
#include "../Documents/FileTransaction.h"
#include <Engine/Assets/AssetDatabase.h>

namespace Editor
{
    struct AssetFileChangePlan
    {
        std::vector<FileChange> files;
        std::string id;
    };
    class AssetFileChanges final
    {
    public:
        /// <summary>素材本体とIDメタデータの変更を準備し、既存の追加情報を生のまま保持します。</summary>
        static AssetFileChangePlan Prepare(const std::filesystem::path& path, std::optional<std::string> before,
                                           std::string after);
    };
}
