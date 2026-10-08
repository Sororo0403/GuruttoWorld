#pragma once
#include <SceneRuntime/SceneLayout.h>

namespace Editor
{
    class SceneSnapshotCache final
    {
    public:
        const std::string& Get(const SceneRuntime::SceneLayout& layout)
        {
            if (!valid_ || saved_.settings!=layout.settings || saved_.assetReferences!=layout.assetReferences ||
                saved_.objects.size()!=layout.objects.size() || !std::equal(saved_.objects.begin(),saved_.objects.end(),layout.objects.begin(),
                    [](const auto& a,const auto& b) { return a.id==b.id && a.name==b.name && a.parentId==b.parentId && a.prefab==b.prefab &&
                        a.position==b.position && a.rotation==b.rotation && a.scale==b.scale && a.SameComponents(b); }))
            {
                auto json=layout.Serialize();
                saved_=layout; json_=std::move(json); valid_=true; ++serializations_;
            }
            return json_;
        }
        size_t Serializations() const { return serializations_; }
    private:
        SceneRuntime::SceneLayout saved_;
        std::string json_;
        bool valid_=false;
        size_t serializations_=0;
    };
}
