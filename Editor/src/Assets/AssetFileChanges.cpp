#include "AssetFileChanges.h"

namespace Editor
{
    auto AssetFileChanges::Prepare(const std::filesystem::path& path, std::optional<std::string> before,
                                   std::string after) -> AssetFileChangePlan
    {
        const auto target = std::filesystem::weakly_canonical(std::filesystem::absolute(path));
        AssetFileChangePlan plan;
        plan.files.push_back({target, std::move(before), std::move(after)});
        const auto metadataPath = Engine::AssetDatabase::Sidecar(target);
        const auto metadataBefore = Engine::FileContents::Read(metadataPath);
        auto metadataAfter = metadataBefore;
        if (!metadataAfter)
        {
            metadataAfter = Engine::AssetDatabase::SerializeMetadata(Engine::AssetDatabase::CreateMetadata());
        }
        plan.id = Engine::AssetDatabase::ParseMetadata(*metadataAfter).id;
        plan.files.push_back({metadataPath, metadataBefore, metadataAfter});
        return plan;
    }
}
