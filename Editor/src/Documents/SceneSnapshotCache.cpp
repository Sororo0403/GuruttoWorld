#include "SceneSnapshotCache.h"

namespace Editor
{
    auto SceneSnapshotCache::Get(const SceneRuntime::SceneLayout& layout) -> const std::string&
    {
        if (!valid_ || saved_.settings != layout.settings || saved_.assetReferences != layout.assetReferences ||
            saved_.sceneObjects != layout.sceneObjects || saved_.prefabSlots != layout.prefabSlots ||
            saved_.objects.size() != layout.objects.size() ||
            !std::equal(saved_.objects.begin(), saved_.objects.end(), layout.objects.begin(),
                        [](const auto& a, const auto& b)
                        {
                            return a.id == b.id && a.name == b.name && a.parentId == b.parentId &&
                                   a.prefab == b.prefab && a.position == b.position && a.rotation == b.rotation &&
                                   a.scale == b.scale && a.SameComponents(b);
                        }))
        {
            auto json = layout.Serialize();
            saved_ = layout;
            json_ = std::move(json);
            valid_ = true;
            ++serializations_;
        }
        return json_;
    }
}
