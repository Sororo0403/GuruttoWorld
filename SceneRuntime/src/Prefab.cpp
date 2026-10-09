#include <SceneRuntime/Prefab.h>
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include "SceneComponentJson.h"

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
        for (auto& script:object.scripts) script.Remap(ids);
        if (object.button)
        {
            if (object.button->action!="loadScene" && object.button->action!="setState") replace(object.button->target);
            replace(object.button->sound);
        }
    }
    std::string Baseline(ScenePlacement object)
    {
        object.parentId.clear();
        SceneLayout layout; layout.objects.push_back(std::move(object)); return layout.Serialize();
    }
    bool Modified(const ScenePlacement& object,const ScenePlacement& baseline,bool root)
    {
        return object.name!=baseline.name || !object.SameComponents(baseline) || (!root &&
            (object.position!=baseline.position || object.rotation!=baseline.rotation || object.scale!=baseline.scale));
    }
    using Engine::Json;
    Json MergeProperty(const Json& old,const Json& current,const Json& source)
    {
        if (current==old) return source;
        if (!old.is_object() || !current.is_object() || !source.is_object()) return current;
        if (old.contains("type") && current.contains("type") && source.contains("type") &&
            (old.at("type")!=current.at("type") || old.at("type")!=source.at("type"))) return current;
        Json result=source;
        for (const auto& [key,value] : current.items())
        {
            if (!old.contains(key)) result[key]=value;
            else if (!source.contains(key)) { if (value!=old.at(key)) result[key]=value; }
            else result[key]=MergeProperty(old.at(key),value,source.at(key));
        }
        for (const auto& [key,value] : old.items())
        { static_cast<void>(value); if (!current.contains(key)) result.erase(key); }
        return result;
    }
    Json ComponentMap(const ScenePlacement& object)
    {
        Json result=Json::object();
        for (const auto& component : WriteSceneComponents(object)) result[component.at("id").get<std::string>()]=component;
        return result;
    }
    void MergeComponents(ScenePlacement& current,const ScenePlacement& old,const ScenePlacement& source)
    {
        const auto merged=MergeProperty(ComponentMap(old),ComponentMap(current),ComponentMap(source));
        Json array=Json::array();
        for (const auto& [id,component] : merged.items()) { static_cast<void>(id); array.push_back(component); }
        ScenePlacement result;
        ReadSceneComponents(Json{{"components",array}},result,false);
        current.CopyComponents(result);
    }
    Json Properties(const ScenePlacement& object,bool root)
    {
        Json value{{"name",object.name},{"components",ComponentMap(object)}};
        if (!root) { value["position"]=object.position; value["rotation"]=object.rotation; value["scale"]=object.scale; }
        return value;
    }
    ScenePlacement BaselineInScene(const SceneLayout& scene,const ScenePlacement& object)
    {
        if (!object.prefab) throw std::runtime_error("Object is not a prefab instance");
        auto baseline=SceneLayout::Parse(object.prefab->baseline).objects.at(0);
        std::map<std::string,std::string> ids;
        for (const auto& item : scene.objects) if (item.prefab && item.prefab->rootId==object.prefab->rootId)
            ids[item.prefab->sourceId]=item.id;
        Remap(baseline,ids); return baseline;
    }
    std::string EscapePointer(const std::string& key)
    {
        std::string escaped;
        for (const auto c : key) escaped+=c=='~' ? "~0" : c=='/' ? "~1" : std::string(1,c);
        return escaped;
    }
    void Differences(const Json& current,const Json& source,const std::string& path,std::vector<PrefabOverride>& result)
    {
        if (current==source) return;
        if (current.is_object() && source.is_object())
        {
            std::set<std::string> keys;
            for (const auto& [key,value] : current.items()) { static_cast<void>(value); keys.insert(key); }
            for (const auto& [key,value] : source.items()) { static_cast<void>(value); keys.insert(key); }
            for (const auto& key : keys)
            {
                const auto property=path+"/"+EscapePointer(key);
                if (!current.contains(key) || !source.contains(key)) result.push_back({property,current.contains(key) ? current.at(key).dump() : "",source.contains(key) ? source.at(key).dump() : ""});
                else Differences(current.at(key),source.at(key),property,result);
            }
        }
        else result.push_back({path,current.dump(),source.dump()});
    }
    void SetProperties(ScenePlacement& object,const Json& properties)
    {
        object.name=properties.at("name").get<std::string>();
        if (properties.contains("position")) object.position=properties.at("position").get<std::array<float,3>>();
        if (properties.contains("rotation")) object.rotation=properties.at("rotation").get<std::array<float,3>>();
        if (properties.contains("scale")) object.scale=properties.at("scale").get<std::array<float,3>>();
        Json array=Json::array();
        for (const auto& [id,value] : properties.at("components").items()) { static_cast<void>(id); array.push_back(value); }
        ScenePlacement components;
        ReadSceneComponents(Json{{"components",array}},components,false); object.CopyComponents(components);
    }
    std::set<std::filesystem::path>& ActivePrefabs()
    { static thread_local std::set<std::filesystem::path> active; return active; }
    struct RefreshScope
    {
        std::filesystem::path path;
        explicit RefreshScope(const std::filesystem::path& file) : path(std::filesystem::weakly_canonical(file))
        {
            if (ActivePrefabs().size()>=64 || !ActivePrefabs().insert(path).second) throw std::runtime_error("Cyclic or excessively nested prefab dependency");
        }
        ~RefreshScope() { ActivePrefabs().erase(path); }
    };
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
            std::optional<PrefabLink> nested;
            if (object.prefab && object.prefab->rootId==rootId)
                nested=SceneLayout::Parse(object.prefab->baseline).objects.at(0).prefab;
            else if (object.prefab) nested=object.prefab;
            Remap(object,remap); object.id=remap.at(object.id); object.prefab=std::move(nested);
            if (object.prefab && remap.contains(object.prefab->rootId)) object.prefab->rootId=remap.at(object.prefab->rootId);
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
            RefreshScope scope(assetsRoot/path);
            auto source=SceneLayout::Load(assetsRoot/path,assetsRoot);
            Refresh(source,assetsRoot);
            const auto sourceRoot=Root(source);
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
                MergeComponents(*found,old,object);
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
    std::vector<PrefabOverride> Prefab::Overrides(const SceneLayout& scene,const std::string& objectId)
    {
        auto copy=scene; const auto& object=Find(copy,objectId);
        const auto baseline=BaselineInScene(scene,object);
        std::vector<PrefabOverride> result;
        Differences(Properties(object,objectId==object.prefab->rootId),Properties(baseline,objectId==object.prefab->rootId),"",result);
        return result;
    }
    void Prefab::RevertOverride(SceneLayout& scene,const std::string& objectId,const std::string& property)
    {
        auto candidate=scene; auto& object=Find(candidate,objectId);
        const auto differences=Overrides(candidate,objectId);
        const auto found=std::find_if(differences.begin(),differences.end(),[&](const auto& item) { return item.path==property; });
        if (found==differences.end()) throw std::runtime_error("Prefab property is not overridden");
        auto properties=Properties(object,objectId==object.prefab->rootId);
        const Json::json_pointer pointer(property);
        if (found->source.empty()) properties=properties.patch(Json::array({Json{{"op","remove"},{"path",property}}}));
        else properties[pointer]=Json::parse(found->source);
        SetProperties(object,properties);
        if (!object.camera && candidate.settings.mainCamera==object.id) candidate.settings.mainCamera.clear();
        static_cast<void>(candidate.Serialize()); scene=std::move(candidate);
    }
    void Prefab::RevertInstance(SceneLayout& scene,const std::string& rootId,const std::filesystem::path& assetsRoot)
    {
        auto candidate=scene;
        const auto& root=Find(candidate,rootId);
        if (!root.prefab || root.prefab->rootId!=rootId) throw std::runtime_error("Revert requires a prefab root");
        Refresh(candidate,assetsRoot);
        for (auto& object : candidate.objects) if (object.prefab && object.prefab->rootId==rootId)
        {
            const auto source=BaselineInScene(candidate,object);
            SetProperties(object,Properties(source,object.id==rootId));
        }
        if (!candidate.settings.mainCamera.empty() && std::none_of(candidate.objects.begin(),candidate.objects.end(),[&](const auto& object) {
            return object.id==candidate.settings.mainCamera && object.camera;
        })) candidate.settings.mainCamera.clear();
        static_cast<void>(candidate.Serialize()); scene=std::move(candidate);
    }
    void Prefab::ApplyOverride(const SceneLayout& scene,const std::string& objectId,const std::string& property,const std::filesystem::path& assetsRoot)
    {
        auto copy=scene; auto object=Find(copy,objectId);
        const auto overrides=Overrides(scene,objectId);
        if (std::none_of(overrides.begin(),overrides.end(),[&](const auto& item) { return item.path==property; }))
            throw std::runtime_error("Prefab property is not overridden");
        const auto path=object.prefab->asset; const auto sourceId=object.prefab->sourceId;
        auto asset=SceneLayout::Load(assetsRoot/path,assetsRoot);
        Refresh(asset,assetsRoot);
        auto& source=Find(asset,sourceId);
        std::map<std::string,std::string> ids;
        for (const auto& item : scene.objects) if (item.prefab && item.prefab->rootId==object.prefab->rootId) ids[item.id]=item.prefab->sourceId;
        Remap(object,ids);
        const bool root=objectId==object.prefab->rootId;
        const auto current=Properties(object,root);
        auto properties=Properties(source,root);
        const Json::json_pointer pointer(property);
        if (current.contains(pointer)) properties[pointer]=current.at(pointer);
        else properties=properties.patch(Json::array({Json{{"op","remove"},{"path",property}}}));
        SetProperties(source,properties);
        asset.Save(assetsRoot/path);
    }
    SceneLayout Prefab::Variant(const SceneLayout& scene,const std::string& rootId)
    {
        auto copy=scene; const auto& root=Find(copy,rootId);
        if (!root.prefab || root.prefab->rootId!=rootId) throw std::runtime_error("Variant requires a prefab root");
        auto variant=Extract(scene,rootId);
        for (auto& source : variant.objects)
        {
            const auto found=std::find_if(scene.objects.begin(),scene.objects.end(),[&](const auto& item) {
                return item.prefab && item.prefab->rootId==rootId && item.prefab->sourceId==source.id;
            });
            if (found!=scene.objects.end()) { source.prefab=found->prefab; source.prefab->rootId=root.prefab->sourceId; }
        }
        static_cast<void>(variant.Serialize()); return variant;
    }
}
