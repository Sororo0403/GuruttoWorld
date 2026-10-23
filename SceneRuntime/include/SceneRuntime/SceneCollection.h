#pragma once
#include <SceneRuntime/SceneTransforms.h>
#include <SceneRuntime/ObjectReferenceDomain.h>
#include <set>
#include <stdexcept>

namespace SceneRuntime
{
    class SceneCollection final
    {
    public:
        struct Entry { std::string name; std::set<std::string> objects; };
        /// <summary>初期シーンの所属を登録します。</summary>
        void Reset(const SceneLayout& layout,std::string name="Main")
        {
            entries_.clear(); Entry entry{std::move(name),{}};
            if(!layout.sceneObjects.empty())
            {
                for(const auto& [scene,ids]:layout.sceneObjects) entries_.push_back({scene,{ids.begin(),ids.end()}});
                return;
            }
            for(const auto& object:layout.objects) entry.objects.insert(object.id);
            entries_.push_back(std::move(entry));
        }
        /// <summary>ロード済みシーンと所属IDを取得します。</summary>
        const std::vector<Entry>& Entries() const { return entries_; }
        /// <summary>追加シーンのIDと内部参照を変換し、候補へ追加します。</summary>
        SceneLayout Add(const SceneLayout& current,SceneLayout incoming,const std::string& name,bool active=false,const std::set<std::string>& reserved={})
        {
            if(name.empty() || std::any_of(entries_.begin(),entries_.end(),[&](const auto& e){return e.name==name;}))
                throw std::runtime_error("Scene is already loaded or unnamed: "+name);
            incoming.Validate();
            if(!incoming.prefabSlots.empty() || !current.prefabSlots.empty()) throw std::runtime_error("Instantiate prefab external slots before adding a scene");
            auto candidate=current; Entry entry{name,{}}; std::map<std::string,std::string> ids;
            auto used = ObjectReferenceDomain(current);
            used.insert(reserved.begin(), reserved.end());
            auto missing = ObjectReferenceDomain(incoming);
            for (const auto& object : incoming.objects)
                missing.erase(object.id);
            used.insert(missing.begin(), missing.end());
            for(const auto& object:incoming.objects)
            {
                auto id=object.id; size_t suffix=1;
                while(used.contains(id)) id=(object.id.size() <= 100 ? object.id : "scene-object")+"-scene-"+std::to_string(suffix++);
                used.insert(id); ids[object.id]=id; entry.objects.insert(id);
            }
            for (const auto& reference : missing)
            {
                std::string id;
                for (size_t i = 1;; ++i)
                {
                    id = "unresolved-scene-" + std::to_string(i);
                    if (used.insert(id).second)
                        break;
                }
                ids[reference] = id;
            }
            Synchronize(current);
            for(auto& object:incoming.objects)
            {
                Remap(object,ids);
                candidate.objects.push_back(std::move(object));
            }
            if(active)
            {
                candidate.settings=incoming.settings;
                const auto camera=ids.find(candidate.settings.mainCamera);
                if(camera!=ids.end()) candidate.settings.mainCamera=camera->second;
            }
            candidate.assetReferences.insert(incoming.assetReferences.begin(),incoming.assetReferences.end());
            entries_.push_back(std::move(entry)); Store(candidate); return candidate;
        }
        /// <summary>保持対象と子孫を残してシーンを除去し、親変更はワールド姿勢を維持します。</summary>
        SceneLayout Unload(const SceneLayout& current,const std::string& name)
        {
            const auto found=std::find_if(entries_.begin(),entries_.end(),[&](const auto& entry){return entry.name==name;});
            if(found==entries_.end()) throw std::runtime_error("Scene is not loaded: "+name);
            Synchronize(current);
            const auto keep=Retained(current,found->objects);
            auto candidate=current; std::vector<DirectX::XMFLOAT4X4> matrices; std::string error;
            if(!SceneTransforms::Resolve(current,matrices,error)) throw std::runtime_error(error);
            for(size_t index=0;index<candidate.objects.size();++index)
            {
                auto& object=candidate.objects[index];
                if(keep.contains(object.id) && !object.parentId.empty() && !keep.contains(object.parentId))
                {
                    ScenePlacement detached;
                    if(!SceneTransforms::ReadTransform(matrices[index],object,detached)) throw std::runtime_error("Persistent object has an unrepresentable World transform: "+object.id);
                    object=std::move(detached); object.parentId.clear();
                }
            }
            std::erase_if(candidate.objects,[&](const auto& object){return !keep.contains(object.id);});
            if(!keep.contains(candidate.settings.mainCamera)) candidate.settings.mainCamera.clear();
            entries_.erase(found); Store(candidate); return candidate;
        }
    private:
        /// <summary>保持対象と残るシーンに所属する個体の子孫を集めます。</summary>
        static std::set<std::string> Retained(const SceneLayout& layout,const std::set<std::string>& removed)
        {
            std::set<std::string> keep;
            for(const auto& object:layout.objects) if(object.persistent || !removed.contains(object.id)) keep.insert(object.id);
            bool changed=true;
            while(changed) {changed=false;for(const auto& object:layout.objects) if(keep.contains(object.parentId)) changed=keep.insert(object.id).second || changed;}
            return keep;
        }
        /// <summary>追加シーンの個体参照を一括変換します。</summary>
        static void Remap(ScenePlacement& object,const std::map<std::string,std::string>& ids)
        {
            const auto remap=[&](std::string& id){if(const auto found=ids.find(id);found!=ids.end()) id=found->second;};
            remap(object.id);remap(object.parentId);
            if(object.prefab) remap(object.prefab->rootId);
            object.RemapComponentReferences(ids);
        }
        void RestoreSavedOwners(const SceneLayout& layout,std::map<std::string,size_t>& owners)
        {
            for(const auto& object:layout.objects)
            {
                if(object.persistent || owners.contains(object.id)) continue;
                auto owner=std::find_if(entries_.begin(),entries_.end(),[&](const auto& e){
                    const auto saved=layout.sceneObjects.find(e.name);
                    return saved!=layout.sceneObjects.end() && std::find(saved->second.begin(),saved->second.end(),object.id)!=saved->second.end();});
                if(owner!=entries_.end()) {owner->objects.insert(object.id);owners.emplace(object.id,static_cast<size_t>(owner-entries_.begin()));}
            }
        }

        void AssignAncestorOwner(const ScenePlacement& object,const std::map<std::string,const ScenePlacement*>& objects,std::map<std::string,size_t>& owners)
        {
            std::vector<const ScenePlacement*> chain;
            std::set<std::string> visited;
            auto ancestor=&object;
            size_t owner=entries_.size()-1;
            while(ancestor && visited.insert(ancestor->id).second)
            {
                if(const auto found=owners.find(ancestor->id);found!=owners.end()) {owner=found->second;break;}
                chain.push_back(ancestor);
                const auto parent=objects.find(ancestor->parentId);
                ancestor=parent==objects.end()?nullptr:parent->second;
            }
            for(const auto* member:chain) if(!member->persistent)
            {entries_[owner].objects.insert(member->id);owners.emplace(member->id,owner);}
        }

        /// <summary>保存済み所属を先に復元し、新しい個体を並び順によらず祖先またはアクティブシーンへ所属させます。</summary>
        void Synchronize(const SceneLayout& layout)
        {
            std::set<std::string> live;
            std::map<std::string,const ScenePlacement*> objects;
            for(const auto& object:layout.objects) {live.insert(object.id);objects.emplace(object.id,&object);}
            for(auto& entry:entries_) std::erase_if(entry.objects,[&](const auto& id){return !live.contains(id);});
            std::map<std::string,size_t> owners;
            for(size_t index=0;index<entries_.size();++index)
                for(const auto& id:entries_[index].objects) owners.emplace(id,index);
            RestoreSavedOwners(layout,owners);
            for(const auto& object:layout.objects)
            {
                if(object.persistent || owners.contains(object.id) || entries_.empty()) continue;
                AssignAncestorOwner(object,objects,owners);
            }
        }
        /// <summary>所属をシーン保存とUndoの対象へ含めます。</summary>
        void Store(SceneLayout& layout) const
        {
            layout.sceneObjects.clear();
            for(const auto& entry:entries_) layout.sceneObjects[entry.name]={entry.objects.begin(),entry.objects.end()};
        }
        std::vector<Entry> entries_;
    };
}
