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
                                           std::string after)
        {
            const auto target = std::filesystem::weakly_canonical(std::filesystem::absolute(path));
            AssetFileChangePlan plan;
            plan.files.push_back({target, std::move(before), std::move(after)});
            const auto metadataPath = Engine::AssetDatabase::Sidecar(target);
            const auto metadataBefore = Engine::FileContents::Read(metadataPath);
            auto metadataAfter = metadataBefore;
            if (!metadataAfter)
            {
                const auto shared = Engine::ContentFiles::Resolve(metadataPath);
                metadataAfter = Engine::FileContents::Read(shared);
                if (!metadataAfter)
                    metadataAfter = Engine::AssetDatabase::SerializeMetadata(Engine::AssetDatabase::CreateMetadata());
            }
            plan.id = Engine::AssetDatabase::ParseMetadata(*metadataAfter).id;
            plan.files.push_back({metadataPath, metadataBefore, metadataAfter});
            return plan;
        }
    };
}
