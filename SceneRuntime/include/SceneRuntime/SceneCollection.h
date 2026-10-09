#pragma once
#include <SceneRuntime/SceneTransforms.h>
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
            Synchronize(current);
            auto candidate=current; Entry entry{name,{}}; std::map<std::string,std::string> ids;
            std::set<std::string> used=reserved; for(const auto& object:current.objects) used.insert(object.id);
            for(const auto& object:incoming.objects)
            {
                auto id=object.id; size_t suffix=1;
                while(used.contains(id)) id=object.id+"-scene-"+std::to_string(suffix++);
                used.insert(id); ids[object.id]=id; entry.objects.insert(id);
            }
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
        /// <summary>保持対象と子孫を残してシーンを除去し、親変更はWorld姿勢を維持します。</summary>
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
            remap(object.id);remap(object.parentId);if(object.joint) remap(object.joint->target);
            if(object.prefab) remap(object.prefab->rootId);for(auto& script:object.scripts) script.Remap(ids);
        if(object.navAgent) object.navAgent->Remap(ids);
            if(object.ragdoll) object.ragdoll->Remap(ids);
            if(object.button)
            {
                const auto& action=object.button->action;
                if(action!="loadScene" && action!="loadSceneAdditive" && action!="unloadScene" && action!="setState") remap(object.button->target);
                remap(object.button->sound);
            }
        }
        /// <summary>実行中に生成された個体を親またはアクティブシーンへ所属させます。</summary>
        void Synchronize(const SceneLayout& layout)
        {
            for(const auto& object:layout.objects)
            {
                if(object.persistent || std::any_of(entries_.begin(),entries_.end(),[&](const auto& e){return e.objects.contains(object.id);})) continue;
                auto owner=std::find_if(entries_.begin(),entries_.end(),[&](const auto& e){
                    const auto saved=layout.sceneObjects.find(e.name);
                    return saved!=layout.sceneObjects.end() && std::find(saved->second.begin(),saved->second.end(),object.id)!=saved->second.end();});
                if(owner==entries_.end()) owner=std::find_if(entries_.begin(),entries_.end(),[&](const auto& e){return e.objects.contains(object.parentId);});
                if(owner==entries_.end() && !entries_.empty()) owner=std::prev(entries_.end());
                if(owner!=entries_.end()) owner->objects.insert(object.id);
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
