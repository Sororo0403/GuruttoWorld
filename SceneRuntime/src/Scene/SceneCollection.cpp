#include <SceneRuntime/Scene/SceneCollection.h>

namespace SceneRuntime
{
    auto SceneCollection::Reset(const SceneLayout& layout, std::string name) -> void
    {
        entries_.clear();
        Entry entry{std::move(name), {}};
        if (!layout.sceneObjects.empty())
        {
            for (const auto& [scene, ids] : layout.sceneObjects)
                entries_.push_back({scene, {ids.begin(), ids.end()}});
            return;
        }
        for (const auto& object : layout.objects)
            entry.objects.insert(object.id);
        entries_.push_back(std::move(entry));
    }

    auto SceneCollection::Add(const SceneLayout& current, SceneLayout incoming, const std::string& name, bool active,
                              const std::set<std::string>& reserved) -> SceneLayout
    {
        if (name.empty() || std::any_of(entries_.begin(), entries_.end(),
                                        [&](const auto& e)
                                        {
                                            return e.name == name;
                                        }))
            throw std::runtime_error("Scene is already loaded or unnamed: " + name);
        incoming.Validate();
        if (!incoming.prefabSlots.empty() || !current.prefabSlots.empty())
            throw std::runtime_error("Instantiate prefab external slots before adding a scene");
        auto candidate = current;
        Entry entry{name, {}};
        std::map<std::string, std::string> ids;
        auto used = ObjectReferenceDomain(current);
        used.insert(reserved.begin(), reserved.end());
        auto missing = ObjectReferenceDomain(incoming);
        for (const auto& object : incoming.objects)
            missing.erase(object.id);
        used.insert(missing.begin(), missing.end());
        for (const auto& object : incoming.objects)
        {
            auto id = object.id;
            size_t suffix = 1;
            while (used.contains(id))
                id = (object.id.size() <= 100 ? object.id : "scene-object") + "-scene-" + std::to_string(suffix++);
            used.insert(id);
            ids[object.id] = id;
            entry.objects.insert(id);
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
        for (auto& object : incoming.objects)
        {
            Remap(object, ids);
            candidate.objects.push_back(std::move(object));
        }
        if (active)
        {
            candidate.settings = incoming.settings;
            const auto camera = ids.find(candidate.settings.mainCamera);
            if (camera != ids.end())
                candidate.settings.mainCamera = camera->second;
        }
        candidate.assetReferences.insert(incoming.assetReferences.begin(), incoming.assetReferences.end());
        entries_.push_back(std::move(entry));
        Store(candidate);
        return candidate;
    }

    auto SceneCollection::Unload(const SceneLayout& current, const std::string& name) -> SceneLayout
    {
        const auto found = std::find_if(entries_.begin(), entries_.end(),
                                        [&](const auto& entry)
                                        {
                                            return entry.name == name;
                                        });
        if (found == entries_.end())
            throw std::runtime_error("Scene is not loaded: " + name);
        Synchronize(current);
        const auto keep = Retained(current, found->objects);
        auto candidate = current;
        std::vector<DirectX::XMFLOAT4X4> matrices;
        std::string error;
        if (!SceneTransforms::Resolve(current, matrices, error))
            throw std::runtime_error(error);
        for (size_t index = 0; index < candidate.objects.size(); ++index)
        {
            auto& object = candidate.objects[index];
            if (keep.contains(object.id) && !object.parentId.empty() && !keep.contains(object.parentId))
            {
                ScenePlacement detached;
                if (!SceneTransforms::ReadTransform(matrices[index], object, detached))
                    throw std::runtime_error("Persistent object has an unrepresentable World transform: " + object.id);
                object = std::move(detached);
                object.parentId.clear();
            }
        }
        std::erase_if(candidate.objects,
                      [&](const auto& object)
                      {
                          return !keep.contains(object.id);
                      });
        if (!keep.contains(candidate.settings.mainCamera))
            candidate.settings.mainCamera.clear();
        entries_.erase(found);
        Store(candidate);
        return candidate;
    }

    auto SceneCollection::Retained(const SceneLayout& layout, const std::set<std::string>& removed)
        -> std::set<std::string>
    {
        std::set<std::string> keep;
        for (const auto& object : layout.objects)
            if (object.persistent || !removed.contains(object.id))
                keep.insert(object.id);
        bool changed = true;
        while (changed)
        {
            changed = false;
            for (const auto& object : layout.objects)
                if (keep.contains(object.parentId))
                    changed = keep.insert(object.id).second || changed;
        }
        return keep;
    }

    auto SceneCollection::Remap(ScenePlacement& object, const std::map<std::string, std::string>& ids) -> void
    {
        const auto remap = [&](std::string& id)
        {
            if (const auto found = ids.find(id); found != ids.end())
                id = found->second;
        };
        remap(object.id);
        remap(object.parentId);
        if (object.prefab)
            remap(object.prefab->rootId);
        object.RemapComponentReferences(ids);
    }

    auto SceneCollection::RestoreSavedOwners(const SceneLayout& layout, std::map<std::string, size_t>& owners) -> void
    {
        for (const auto& object : layout.objects)
        {
            if (object.persistent || owners.contains(object.id))
                continue;
            auto owner = std::find_if(entries_.begin(), entries_.end(),
                                      [&](const auto& e)
                                      {
                                          const auto saved = layout.sceneObjects.find(e.name);
                                          return saved != layout.sceneObjects.end() &&
                                                 std::find(saved->second.begin(), saved->second.end(), object.id) !=
                                                     saved->second.end();
                                      });
            if (owner != entries_.end())
            {
                owner->objects.insert(object.id);
                owners.emplace(object.id, static_cast<size_t>(owner - entries_.begin()));
            }
        }
    }

    auto SceneCollection::AssignAncestorOwner(const ScenePlacement& object,
                                              const std::map<std::string, const ScenePlacement*>& objects,
                                              std::map<std::string, size_t>& owners) -> void
    {
        std::vector<const ScenePlacement*> chain;
        std::set<std::string> visited;
        auto ancestor = &object;
        size_t owner = entries_.size() - 1;
        while (ancestor && visited.insert(ancestor->id).second)
        {
            if (const auto found = owners.find(ancestor->id); found != owners.end())
            {
                owner = found->second;
                break;
            }
            chain.push_back(ancestor);
            const auto parent = objects.find(ancestor->parentId);
            ancestor = parent == objects.end() ? nullptr : parent->second;
        }
        for (const auto* member : chain)
            if (!member->persistent)
            {
                entries_[owner].objects.insert(member->id);
                owners.emplace(member->id, owner);
            }
    }

    auto SceneCollection::Synchronize(const SceneLayout& layout) -> void
    {
        std::set<std::string> live;
        std::map<std::string, const ScenePlacement*> objects;
        for (const auto& object : layout.objects)
        {
            live.insert(object.id);
            objects.emplace(object.id, &object);
        }
        for (auto& entry : entries_)
            std::erase_if(entry.objects,
                          [&](const auto& id)
                          {
                              return !live.contains(id);
                          });
        std::map<std::string, size_t> owners;
        for (size_t index = 0; index < entries_.size(); ++index)
            for (const auto& id : entries_[index].objects)
                owners.emplace(id, index);
        RestoreSavedOwners(layout, owners);
        for (const auto& object : layout.objects)
        {
            if (object.persistent || owners.contains(object.id) || entries_.empty())
                continue;
            AssignAncestorOwner(object, objects, owners);
        }
    }

    auto SceneCollection::Store(SceneLayout& layout) const -> void
    {
        layout.sceneObjects.clear();
        for (const auto& entry : entries_)
            layout.sceneObjects[entry.name] = {entry.objects.begin(), entry.objects.end()};
    }
}
