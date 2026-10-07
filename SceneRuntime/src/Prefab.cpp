#include <SceneRuntime/Prefab.h>
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

namespace
{
    using namespace SceneRuntime;
    ScenePlacement& Find(SceneLayout& layout,const std::string& id)
    {
        const auto found=std::find_if(layout.objects.begin(),layout.objects.end(),[&](const auto& object) { return object.id==id; });
        if (found==layout.objects.end()) throw std::runtime_error("Prefab object missing: "+id);
        return *found;
    }
    std::string Root(const SceneLayout& layout)
    {
        std::string root;
        for (const auto& object : layout.objects) if (object.parentId.empty())
        { if (!root.empty()) throw std::runtime_error("Prefab requires a single root"); root=object.id; }
        if (root.empty()) throw std::runtime_error("Prefab is empty");
        return root;
    }
    std::string Unique(const SceneLayout& scene,const std::string& base)
    {
        std::set<std::string> ids;
        for (const auto& object : scene.objects) ids.insert(object.id);
        for (size_t index=1;;++index) { const auto id=base+"-"+std::to_string(index); if (!ids.contains(id)) return id; }
    }
    void Remap(ScenePlacement& object,const std::map<std::string,std::string>& ids)
    {
        const auto replace=[&](std::string& id) { const auto found=ids.find(id); if (found!=ids.end()) id=found->second; };
        replace(object.parentId);
        if (object.button)
        {
            if (object.button->action!="loadScene" && object.button->action!="setState") replace(object.button->target);
            replace(object.button->sound);
        }
    }
    std::string Baseline(ScenePlacement object)
    {
        object.parentId.clear(); object.prefab.reset();
        SceneLayout layout; layout.objects.push_back(std::move(object)); return layout.Serialize();
    }
    bool Modified(const ScenePlacement& object,const ScenePlacement& baseline,bool root)
    {
        return object.name!=baseline.name || !object.SameComponents(baseline) || (!root &&
            (object.position!=baseline.position || object.rotation!=baseline.rotation || object.scale!=baseline.scale));
    }
}
namespace SceneRuntime
{
    bool Prefab::ValidPath(const std::filesystem::path& path)
    {
        const auto utf8=path.generic_u8string();
        const std::string value(utf8.begin(),utf8.end());
        return !path.is_absolute() && !path.has_root_name() && value.starts_with("Assets/Prefabs/") && path.extension()==".prefab" &&
            std::none_of(path.begin(),path.end(),[](const auto& part) { return part==".."; });
    }
    SceneLayout Prefab::Extract(const SceneLayout& scene,const std::string& rootId)
    {
        auto copy=scene; const auto& root=Find(copy,rootId);
        std::set<std::string> selected{root.id};
        bool changed=true;
        while (changed)
        {
            changed=false;
            for (const auto& object : scene.objects)
                if (selected.contains(object.parentId) && selected.insert(object.id).second) changed=true;
        }
        SceneLayout asset; std::map<std::string,std::string> remap;
        for (const auto& object : scene.objects) if (selected.contains(object.id))
            remap[object.id]=object.prefab && object.prefab->rootId==rootId ? object.prefab->sourceId : object.id;
        for (auto object : scene.objects) if (selected.contains(object.id))
        {
            const bool isRoot=object.id==rootId;
            Remap(object,remap); object.id=remap.at(object.id); object.prefab.reset();
            if (isRoot) { object.parentId.clear(); object.position={0,0,0}; }
            asset.objects.push_back(std::move(object));
        }
        static_cast<void>(asset.Serialize()); return asset;
    }
    std::string Prefab::Instantiate(SceneLayout& scene,const SceneLayout& asset,const std::filesystem::path& path,const std::array<float,3>& position)
    {
        if (!ValidPath(path)) throw std::runtime_error("Invalid prefab path");
        static_cast<void>(asset.Serialize()); const auto sourceRoot=Root(asset);
        auto candidate=scene; std::map<std::string,std::string> ids;
        const auto root=Unique(candidate,"prefab");
        for (const auto& object : asset.objects) ids[object.id]=object.id==sourceRoot ? root : root+"-"+object.id;
        for (auto object : asset.objects)
        {
            const auto source=object.id; const auto baseline=Baseline(object);
            Remap(object,ids); object.id=ids.at(source);
            object.prefab=PrefabLink{path,source,root,baseline};
            if (source==sourceRoot) object.position=position;
            candidate.objects.push_back(std::move(object));
        }
        static_cast<void>(candidate.Serialize()); scene=std::move(candidate); return root;
    }
    void Prefab::Refresh(SceneLayout& scene,const std::filesystem::path& assetsRoot)
    {
        auto candidate=scene;
        std::map<std::string,std::filesystem::path> roots;
        for (const auto& object : candidate.objects) if (object.prefab) roots.emplace(object.prefab->rootId,object.prefab->asset);
        for (const auto& [rootId,path] : roots)
        {
            auto root=std::find_if(candidate.objects.begin(),candidate.objects.end(),[&](const auto& object) { return object.id==rootId; });
            if (root==candidate.objects.end() || !root->prefab) { Unpack(candidate,rootId); continue; }
            if (!ValidPath(path)) throw std::runtime_error("Invalid prefab path");
            const auto source=SceneLayout::Load(assetsRoot/path); const auto sourceRoot=Root(source);
            std::map<std::string,std::string> ids;
            for (const auto& object : candidate.objects) if (object.prefab && object.prefab->rootId==rootId)
            {
                if (object.prefab->asset!=path || !ids.emplace(object.prefab->sourceId,object.id).second) throw std::runtime_error("Invalid prefab instance links");
            }
            ids[sourceRoot]=rootId;
            for (const auto& object : source.objects) if (!ids.contains(object.id)) ids[object.id]=Unique(candidate,rootId+"-"+object.id);
            std::set<std::string> live;
            for (auto object : source.objects)
            {
                const auto sourceId=object.id; live.insert(sourceId);
                const auto baseline=Baseline(object); Remap(object,ids); object.id=ids.at(sourceId);
                const auto found=std::find_if(candidate.objects.begin(),candidate.objects.end(),[&](const auto& value) { return value.id==object.id; });
                if (found==candidate.objects.end())
                { object.prefab=PrefabLink{path,sourceId,rootId,baseline}; candidate.objects.push_back(std::move(object)); continue; }
                if (!found->prefab) throw std::runtime_error("Prefab update conflicts with an existing object ID");
                auto old=SceneLayout::Parse(found->prefab->baseline).objects.at(0); Remap(old,ids);
                const bool isRoot=found->id==rootId;
                if (found->name==old.name) found->name=object.name;
                if (found->SameComponents(old)) found->CopyComponents(object);
                if (!isRoot)
                {
                    if (found->position==old.position) found->position=object.position;
                    if (found->rotation==old.rotation) found->rotation=object.rotation;
                    if (found->scale==old.scale) found->scale=object.scale;
                    // Parent links are structural; source changes restore the source hierarchy.
                    found->parentId=object.parentId;
                }
                found->prefab=PrefabLink{path,sourceId,rootId,baseline};
            }
            std::set<std::string> removed;
            for (auto& object : candidate.objects) if (object.prefab && object.prefab->rootId==rootId && !live.contains(object.prefab->sourceId))
            {
                auto old=SceneLayout::Parse(object.prefab->baseline).objects.at(0); Remap(old,ids);
                if (Modified(object,old,object.id==rootId)) object.prefab.reset();
                else removed.insert(object.id);
            }
            for (auto& object : candidate.objects) if (removed.contains(object.parentId)) object.parentId=rootId;
            std::erase_if(candidate.objects,[&](const auto& object) { return removed.contains(object.id); });
        }
        static_cast<void>(candidate.Serialize()); scene=std::move(candidate);
    }
    void Prefab::Bind(SceneLayout& scene,const std::string& rootId,const std::filesystem::path& path)
    {
        if (!ValidPath(path)) throw std::runtime_error("Invalid prefab path");
        const auto asset=Extract(scene,rootId);
        for (const auto& source : asset.objects)
        {
            const auto found=std::find_if(scene.objects.begin(),scene.objects.end(),[&](const auto& object) {
                return object.id==source.id || (object.prefab && object.prefab->rootId==rootId && object.prefab->sourceId==source.id);
            });
            if (found==scene.objects.end()) throw std::runtime_error("Prefab bind object missing");
            found->prefab=PrefabLink{path,source.id,rootId,Baseline(source)};
        }
    }
    void Prefab::Unpack(SceneLayout& scene,const std::string& rootId)
    { for (auto& object : scene.objects) if (object.prefab && object.prefab->rootId==rootId) object.prefab.reset(); }
}
