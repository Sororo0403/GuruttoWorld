#pragma once
#include <SceneRuntime/SceneLayout.h>
#include <SceneRuntime/ScriptRuntime.h>

namespace Editor
{
    class SceneSnapshotCache final
    {
    public:
        const std::string& Get(const SceneRuntime::SceneLayout& layout)
        {
            const auto revision = SceneRuntime::ScriptRegistry::Revision();
            if (!valid_ || revision_ != revision || saved_.settings != layout.settings ||
                saved_.assetReferences != layout.assetReferences || saved_.sceneObjects != layout.sceneObjects ||
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
                revision_ = revision;
                valid_ = true;
                ++serializations_;
            }
            return json_;
        }
        size_t Serializations() const
        {
            return serializations_;
        }

    private:
        SceneRuntime::SceneLayout saved_;
        std::string json_;
        bool valid_ = false;
        size_t serializations_ = 0;
        std::uint64_t revision_ = 0;
    };
}
