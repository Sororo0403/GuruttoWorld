#include <SceneRuntime/Prefab.h>
#include <SceneRuntime/SceneAssetReferences.h>
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include "SceneComponentJson.h"
#include "PrefabMerge.h"
#include <Engine/Assets/AssetDatabase.h>

namespace
{
    using namespace SceneRuntime;
    ScenePlacement& Find(SceneLayout& layout, const std::string& id)
    {
        const auto found = std::find_if(layout.objects.begin(), layout.objects.end(),
                                        [&](const auto& object)
                                        {
                                            return object.id == id;
                                        });
        if (found == layout.objects.end())
            throw std::runtime_error("Prefab object missing: " + id);
        return *found;
    }
    const ScenePlacement& Find(const SceneLayout& layout, const std::string& id)
    {
        const auto found = std::find_if(layout.objects.begin(), layout.objects.end(),
                                        [&](const auto& object)
                                        {
                                            return object.id == id;
                                        });
        if (found == layout.objects.end())
            throw std::runtime_error("Prefab object missing: " + id);
        return *found;
    }
    std::string Root(const SceneLayout& layout)
    {
        std::string root;
        for (const auto& object : layout.objects)
            if (object.parentId.empty())
            {
                if (!root.empty())
                    throw std::runtime_error("Prefab requires a single root");
                root = object.id;
            }
        if (root.empty())
            throw std::runtime_error("Prefab is empty");
        return root;
    }
    std::string Unique(const SceneLayout& scene, const std::string& base)
    {
        std::set<std::string> ids;
        for (const auto& object : scene.objects)
            ids.insert(object.id);
        for (size_t index = 1;; ++index)
        {
            const auto id = base + "-" + std::to_string(index);
            if (!ids.contains(id))
                return id;
        }
    }
    void Remap(ScenePlacement& object, const std::map<std::string, std::string>& ids)
    {
        const auto replace = [&](std::string& id)
        {
            const auto found = ids.find(id);
            if (found != ids.end())
                id = found->second;
        };
        replace(object.parentId);
        object.RemapComponentReferences(ids);
    }
    std::map<std::string, std::string> ReferenceMap(std::map<std::string, std::string> ids,
                                                    const PrefabBindings& bindings)
    {
        for (const auto& [slot, target] : bindings)
            if (!ids.emplace(slot, target.id).second)
                throw std::runtime_error("Prefab slot conflicts with an internal object: " + slot);
        return ids;
    }
    void ValidateBindings(const SceneLayout& scene, const PrefabBindings& bindings)
    {
        for (const auto& [slot, target] : bindings)
        {
            static_cast<void>(slot);
            if (!target.id.empty() && !scene.prefabSlots.contains(target.id) &&
                std::none_of(scene.objects.begin(), scene.objects.end(),
                             [&](const auto& object)
                             {
                                 return object.id == target.id;
                             }))
                throw std::runtime_error("Prefab binding target is missing: " + target.id);
        }
    }
    std::string Baseline(ScenePlacement object, const std::map<std::string, std::string>& slots = {},
                         const std::map<std::string, std::string>& references = {})
    {
        object.parentId.clear();
        SceneLayout layout;
        layout.objects.push_back(std::move(object));
        layout.prefabSlots = slots;
        InheritSceneAssetReferences(layout, references);
        return layout.Serialize();
    }
    bool Modified(const ScenePlacement& object, const ScenePlacement& baseline, bool root)
    {
        return object.name != baseline.name || !object.SameComponents(baseline) ||
               (!root && (object.position != baseline.position || object.rotation != baseline.rotation ||
                          object.scale != baseline.scale));
    }
    using Engine::Json;
    Json ComponentMap(const ScenePlacement& object)
    {
        Json result = Json::object();
        for (const auto& component : WriteSceneComponents(object))
            result[component.at("id").get<std::string>()] = component;
        return result;
    }
    void MergeComponents(ScenePlacement& current, const ScenePlacement& old, const ScenePlacement& source)
    {
        const auto merged = PrefabMerge::Merge(ComponentMap(old), ComponentMap(current), ComponentMap(source));
        Json array = Json::array();
        for (const auto& [id, component] : merged.items())
        {
            static_cast<void>(id);
            array.push_back(component);
        }
        ScenePlacement result;
        ReadSceneComponents(Json{{"components", array}}, result, false);
        result.persistent = current.persistent == old.persistent ? source.persistent : current.persistent;
        current.CopyComponents(result);
    }
    Json Properties(const ScenePlacement& object, bool root)
    {
        Json value{{"name", object.name}, {"persistent", object.persistent}, {"components", ComponentMap(object)}};
        if (!root)
        {
            value["position"] = object.position;
            value["rotation"] = object.rotation;
            value["scale"] = object.scale;
        }
        return value;
    }
    ScenePlacement BaselineInScene(const SceneLayout& scene, const ScenePlacement& object)
    {
        if (!object.prefab)
            throw std::runtime_error("Object is not a prefab instance");
        auto baseline = SceneLayout::Parse(object.prefab->baseline).objects.at(0);
        std::map<std::string, std::string> ids;
        for (const auto& item : scene.objects)
            if (item.prefab && item.prefab->rootId == object.prefab->rootId)
                ids[item.prefab->sourceId] = item.id;
        const auto& root = Find(scene, object.prefab->rootId);
        if (root.prefab && root.prefab->referenceVersion != 0)
            ids = ReferenceMap(std::move(ids), root.prefab->externalBindings);
        Remap(baseline, ids);
        return baseline;
    }
    std::string EscapePointer(const std::string& key)
    {
        std::string escaped;
        for (const auto c : key)
            escaped += c == '~' ? "~0" : c == '/' ? "~1" : std::string(1, c);
        return escaped;
    }
    void Differences(const Json& current, const Json& source, const std::string& path,
                     std::vector<PrefabOverride>& result)
    {
        if (current == source)
            return;
        if (current.is_object() && source.is_object())
        {
            std::set<std::string> keys;
            for (const auto& [key, value] : current.items())
            {
                static_cast<void>(value);
                keys.insert(key);
            }
            for (const auto& [key, value] : source.items())
            {
                static_cast<void>(value);
                keys.insert(key);
            }
            for (const auto& key : keys)
            {
                const auto property = path + "/" + EscapePointer(key);
                if (!current.contains(key) || !source.contains(key))
                    result.push_back({property, current.contains(key) ? current.at(key).dump() : "",
                                      source.contains(key) ? source.at(key).dump() : ""});
                else
                    Differences(current.at(key), source.at(key), property, result);
            }
        }
        else
            result.push_back({path, current.dump(), source.dump()});
    }
    void SetProperties(ScenePlacement& object, const Json& properties)
    {
        object.name = properties.at("name").get<std::string>();
        if (properties.contains("position"))
            object.position = properties.at("position").get<std::array<float, 3>>();
        if (properties.contains("rotation"))
            object.rotation = properties.at("rotation").get<std::array<float, 3>>();
        if (properties.contains("scale"))
            object.scale = properties.at("scale").get<std::array<float, 3>>();
        Json array = Json::array();
        for (const auto& [id, value] : properties.at("components").items())
        {
            static_cast<void>(id);
            array.push_back(value);
        }
        ScenePlacement components;
        ReadSceneComponents(Json{{"components", array}}, components, false);
        components.persistent = properties.at("persistent").get<bool>();
        object.CopyComponents(components);
    }
    std::set<std::filesystem::path>& ActivePrefabs()
    {
        static thread_local std::set<std::filesystem::path> active;
        return active;
    }
    struct RefreshScope
    {
        std::filesystem::path path;
        explicit RefreshScope(const std::filesystem::path& file) : path(std::filesystem::weakly_canonical(file))
        {
            if (ActivePrefabs().size() >= 64 || !ActivePrefabs().insert(path).second)
                throw std::runtime_error("Cyclic or excessively nested prefab dependency");
        }
        ~RefreshScope()
        {
            ActivePrefabs().erase(path);
        }
    };
}
namespace SceneRuntime
{
    bool Prefab::ValidPath(const std::filesystem::path& path)
    {
        return Engine::AssetDatabase::Valid(path) && Engine::AssetDatabase::Text(path).starts_with("Assets/Prefabs/") &&
               path.extension() == ".prefab";
    }
    static std::set<std::string> Descendants(const SceneLayout& scene, const std::string& rootId)
    {
        std::set<std::string> selected{rootId};
        bool changed = true;
        while (changed)
        {
            changed = false;
            for (const auto& object : scene.objects)
                if (selected.contains(object.parentId) && selected.insert(object.id).second)
                    changed = true;
        }
        return selected;
    }
    static std::set<std::string> ReserveExtractionIds(const SceneLayout& scene, const std::set<std::string>& selected,
                                                      const std::string& rootId,
                                                      const PrefabBindings& inheritedBindings,
                                                      std::map<std::string, std::string>& remap,
                                                      std::map<std::string, std::string>& sourceToScene)
    {
        std::set<std::string> reserved;
        for (const auto& object : scene.objects)
        {
            reserved.insert(object.id);
            if (object.prefab)
                reserved.insert(object.prefab->sourceId);
            if (selected.contains(object.id))
            {
                const bool owned = object.prefab && object.prefab->rootId == rootId;
                remap[object.id] = owned ? object.prefab->sourceId : object.id;
                if (owned)
                    sourceToScene[object.prefab->sourceId] = object.id;
            }
        }
        for (const auto& [slot, target] : inheritedBindings)
        {
            static_cast<void>(target);
            reserved.insert(slot);
        }
        for (auto object : scene.objects)
            object.VisitObjectReferences(
                [&](std::string& id)
                {
                    reserved.insert(id);
                });
        return reserved;
    }
    static void BuildSymbolicBindings(const PrefabBindings& inheritedBindings, std::set<std::string>& reserved,
                                      PrefabBindings& symbolicBindings,
                                      std::map<std::string, std::string>& symbolicSlots)
    {
        for (const auto& [slot, target] : inheritedBindings)
        {
            static_cast<void>(target);
            std::string symbol;
            for (size_t i = 1;; ++i)
            {
                symbol = "prefab-scope-marker-" + std::to_string(i);
                if (!reserved.contains(symbol))
                    break;
            }
            reserved.insert(symbol);
            symbolicBindings[slot] = {symbol};
            symbolicSlots[symbol] = slot;
        }
    }
    static void RestoreExtractedPrefab(ScenePlacement& object, const std::string& rootId,
                                       const std::map<std::string, std::string>& sourceToScene,
                                       const PrefabBindings& inheritedBindings,
                                       const std::map<std::string, std::string>& symbolicIds, SceneLayout& asset)
    {
        if (object.prefab && object.prefab->rootId == rootId)
        {
            const auto originalLayout = SceneLayout::Parse(object.prefab->baseline);
            const auto original = originalLayout.objects.at(0);
            if (original.prefab)
            {
                const auto key = Engine::AssetDatabase::Text(original.prefab->asset);
                if (originalLayout.assetReferences.contains(key))
                {
                    const auto& id = originalLayout.assetReferences.at(key);
                    if (asset.assetReferences.contains(key) && asset.assetReferences.at(key) != id)
                        throw std::runtime_error("Resolve asset identities before extracting prefab: " + key);
                    asset.assetReferences[key] = id;
                }
            }
            auto bound = original, symbolic = original;
            Remap(bound, ReferenceMap(sourceToScene, inheritedBindings));
            Remap(symbolic, symbolicIds);
            // 接続先が同じ二つの口も、継承値は基準の口IDへ戻して区別を保ちます。
            MergeComponents(object, bound, symbolic);
            object.prefab = original.prefab;
            if (object.prefab)
            {
                if (sourceToScene.contains(object.prefab->rootId))
                    object.prefab->rootId = sourceToScene.at(object.prefab->rootId);
                for (auto& [slot, target] : object.prefab->externalBindings)
                {
                    static_cast<void>(slot);
                    if (symbolicIds.contains(target.id))
                        target.id = symbolicIds.at(target.id);
                }
            }
        }
    }
    static std::map<std::string, std::string> CaptureExternalBindings(
        SceneLayout& asset, const SceneLayout& scene, const std::set<std::string>& selected,
        const std::map<std::string, std::string>& symbolicSlots, const PrefabBindings& inheritedBindings,
        const std::map<std::string, std::string>& inheritedLabels, std::set<std::string>& reserved,
        PrefabBindings& bindings)
    {
        std::map<std::string, std::string> outside;
        for (auto& object : asset.objects)
            object.VisitObjectReferences(
                [&](std::string& id)
                {
                    if (id.empty() || selected.contains(id))
                        return;
                    if (symbolicSlots.contains(id))
                    {
                        const auto& slot = symbolicSlots.at(id);
                        outside[id] = slot;
                        bindings[slot] = inheritedBindings.at(slot);
                        asset.prefabSlots[slot] = inheritedLabels.contains(slot) ? inheritedLabels.at(slot) : slot;
                    }
                    else if (!outside.contains(id))
                    {
                        std::string slot;
                        for (size_t i = 1;; ++i)
                        {
                            slot = "prefab-external-" + std::to_string(i);
                            if (!reserved.contains(slot))
                                break;
                        }
                        reserved.insert(slot);
                        outside[id] = slot;
                        bindings[slot] = {id};
                        asset.prefabSlots[slot] = scene.prefabSlots.contains(id) ? scene.prefabSlots.at(id) : id;
                    }
                });
        return outside;
    }
    static std::pair<SceneLayout, PrefabBindings> ExtractScoped(const SceneLayout& scene, const std::string& rootId)
    {
        const auto& root = Find(scene, rootId);
        const auto selected = Descendants(scene, rootId);
        SceneLayout asset;
        asset.assetReferences = scene.assetReferences;
        PrefabBindings bindings;
        const auto inheritedLabels =
            root.prefab ? SceneLayout::Parse(root.prefab->baseline).prefabSlots : std::map<std::string, std::string>{};
        const auto inheritedBindings =
            root.prefab && root.prefab->rootId == rootId && root.prefab->referenceVersion != 0
                ? root.prefab->externalBindings
                : PrefabBindings{};
        std::map<std::string, std::string> remap, sourceToScene;
        auto reserved = ReserveExtractionIds(scene, selected, rootId, inheritedBindings, remap, sourceToScene);
        PrefabBindings symbolicBindings;
        std::map<std::string, std::string> symbolicSlots;
        BuildSymbolicBindings(inheritedBindings, reserved, symbolicBindings, symbolicSlots);
        // 抽出中だけ識別子の領域を分け、実在するScene IDを口IDと誤認しないようにします。
        const auto symbolicIds = ReferenceMap(sourceToScene, symbolicBindings);
        for (auto object : scene.objects)
            if (selected.contains(object.id))
            {
                RestoreExtractedPrefab(object, rootId, sourceToScene, inheritedBindings, symbolicIds, asset);
                asset.objects.push_back(std::move(object));
            }
        const auto outside = CaptureExternalBindings(asset, scene, selected, symbolicSlots, inheritedBindings,
                                                     inheritedLabels, reserved, bindings);
        for (auto& object : asset.objects)
        {
            const bool isRoot = object.id == rootId;
            object.VisitObjectReferences(
                [&](std::string& id)
                {
                    if (outside.contains(id))
                        id = outside.at(id);
                });
            Remap(object, remap);
            object.id = remap.at(object.id);
            if (object.prefab && remap.contains(object.prefab->rootId))
                object.prefab->rootId = remap.at(object.prefab->rootId);
            if (isRoot)
            {
                object.parentId.clear();
                object.position = {0, 0, 0};
            }
        }
        InheritSceneAssetReferences(asset, {});
        asset.Validate();
        return {std::move(asset), std::move(bindings)};
    }
    SceneLayout Prefab::Extract(const SceneLayout& scene, const std::string& rootId)
    {
        return ExtractScoped(scene, rootId).first;
    }
    std::map<std::string, std::string> Prefab::ExternalSlots(const SceneLayout& asset)
    {
        auto slots = asset.prefabSlots;
        std::set<std::string> internal;
        for (const auto& object : asset.objects)
            internal.insert(object.id);
        for (auto object : asset.objects)
            object.VisitObjectReferences(
                [&](std::string& id)
                {
                    if (!id.empty() && !internal.contains(id))
                        slots.try_emplace(id, id);
                });
        return slots;
    }
    PrefabBindings Prefab::ExternalBindings(const SceneLayout& scene, const std::string& rootId)
    {
        const auto& root = Find(scene, rootId);
        if (!root.prefab || root.prefab->rootId != rootId)
            throw std::runtime_error("Prefab root missing");
        if (root.prefab->referenceVersion != 0)
            return root.prefab->externalBindings;
        SceneLayout original;
        for (const auto& object : scene.objects)
            if (object.prefab && object.prefab->rootId == rootId)
            {
                auto source = SceneLayout::Parse(object.prefab->baseline).objects.at(0);
                source.parentId.clear();
                original.objects.push_back(std::move(source));
            }
        auto bindings = root.prefab->referenceVersion == 0 ? PrefabBindings{} : root.prefab->externalBindings;
        for (const auto& [slot, label] : ExternalSlots(original))
        {
            static_cast<void>(label);
            bindings.try_emplace(slot, ScriptObjectReference{});
        }
        return bindings;
    }
    void Prefab::BindReferences(SceneLayout& scene, const std::string& rootId, const PrefabBindings& bindings)
    {
        auto candidate = scene;
        auto expected = ExternalBindings(candidate, rootId);
        for (const auto& [slot, target] : bindings)
        {
            if (!expected.contains(slot))
                throw std::runtime_error("Unknown prefab external slot: " + slot);
            expected[slot] = target;
        }
        ValidateBindings(candidate, bindings);
        const auto& root = Find(candidate, rootId);
        const auto oldBindings = root.prefab->referenceVersion == 0 ? PrefabBindings{} : root.prefab->externalBindings;
        std::map<std::string, std::string> ids;
        for (const auto& object : candidate.objects)
            if (object.prefab && object.prefab->rootId == rootId)
                ids[object.prefab->sourceId] = object.id;
        for (auto& object : candidate.objects)
            if (object.prefab && object.prefab->rootId == rootId)
            {
                auto old = SceneLayout::Parse(object.prefab->baseline).objects.at(0);
                auto updated = old;
                Remap(old, ReferenceMap(ids, oldBindings));
                Remap(updated, ReferenceMap(ids, expected));
                MergeComponents(object, old, updated);
                object.prefab->referenceVersion = 1;
            }
        Find(candidate, rootId).prefab->externalBindings = std::move(expected);
        candidate.Validate();
        scene = std::move(candidate);
    }
    std::string Prefab::Instantiate(SceneLayout& scene, const SceneLayout& asset, const std::filesystem::path& path,
                                    const std::array<float, 3>& position, const PrefabBindings& supplied)
    {
        if (!ValidPath(path))
            throw std::runtime_error("Invalid prefab path");
        static_cast<void>(asset.Serialize());
        const auto sourceRoot = Root(asset);
        PrefabBindings bindings;
        const auto slots = ExternalSlots(asset);
        for (const auto& [slot, label] : slots)
        {
            static_cast<void>(label);
            bindings.emplace(slot, ScriptObjectReference{});
        }
        for (const auto& [slot, target] : supplied)
        {
            if (!bindings.contains(slot))
                throw std::runtime_error("Unknown prefab external slot: " + slot);
            bindings[slot] = target;
        }
        ValidateBindings(scene, bindings);
        auto candidate = scene;
        std::map<std::string, std::string> ids;
        const auto root = Unique(candidate, "prefab");
        std::set<std::string> used{root};
        for (const auto& object : candidate.objects)
            used.insert(object.id);
        for (const auto& object : asset.objects)
        {
            if (object.id == sourceRoot)
            {
                ids[object.id] = root;
                continue;
            }
            const auto base = root + "-" + object.id;
            auto id = base;
            size_t suffix = 1;
            while (used.contains(id))
                id = base + "-" + std::to_string(suffix++);
            used.insert(id);
            ids[object.id] = std::move(id);
        }
        for (auto object : asset.objects)
        {
            const auto source = object.id;
            const auto baseline = Baseline(object, slots, asset.assetReferences);
            Remap(object, ReferenceMap(ids, bindings));
            object.id = ids.at(source);
            object.prefab = PrefabLink{path, source, root, baseline};
            if (source == sourceRoot)
            {
                object.position = position;
                object.prefab->externalBindings = bindings;
            }
            candidate.objects.push_back(std::move(object));
        }
        static_cast<void>(candidate.Serialize());
        scene = std::move(candidate);
        return root;
    }
    static std::set<std::string> MergeInstance(SceneLayout& candidate, const SceneLayout& source,
                                               const std::map<std::string, std::string>& ids, const std::string& rootId,
                                               const std::filesystem::path& path, const PrefabBindings& bindings)
    {
        std::set<std::string> live;
        for (auto object : source.objects)
        {
            const auto sourceId = object.id;
            live.insert(sourceId);
            const auto baseline = Baseline(object, Prefab::ExternalSlots(source), source.assetReferences);
            Remap(object, ReferenceMap(ids, bindings));
            object.id = ids.at(sourceId);
            const auto found = std::find_if(candidate.objects.begin(), candidate.objects.end(),
                                            [&](const auto& value)
                                            {
                                                return value.id == object.id;
                                            });
            if (found == candidate.objects.end())
            {
                object.prefab = PrefabLink{path, sourceId, rootId, baseline};
                candidate.objects.push_back(std::move(object));
                continue;
            }
            if (!found->prefab)
                throw std::runtime_error("Prefab update conflicts with an existing object ID");
            auto old = SceneLayout::Parse(found->prefab->baseline).objects.at(0);
            Remap(old, ReferenceMap(ids, bindings));
            const bool isRoot = found->id == rootId;
            if (found->name == old.name)
                found->name = object.name;
            MergeComponents(*found, old, object);
            if (!isRoot)
            {
                for (size_t channel = 0; channel < found->position.size(); ++channel)
                {
                    if (found->position[channel] == old.position[channel])
                        found->position[channel] = object.position[channel];
                    if (found->rotation[channel] == old.rotation[channel])
                        found->rotation[channel] = object.rotation[channel];
                    if (found->scale[channel] == old.scale[channel])
                        found->scale[channel] = object.scale[channel];
                }
                // 親への参照は構造の一部です。元データの変更時は元の階層を復元します。
                found->parentId = object.parentId;
            }
            found->prefab = PrefabLink{path, sourceId, rootId, baseline};
        }
        return live;
    }

    static void RemoveDeletedSources(SceneLayout& candidate, const std::set<std::string>& live,
                                     const std::map<std::string, std::string>& ids, const std::string& rootId)
    {
        std::set<std::string> removed;
        for (auto& object : candidate.objects)
            if (object.prefab && object.prefab->rootId == rootId && !live.contains(object.prefab->sourceId))
            {
                auto old = SceneLayout::Parse(object.prefab->baseline).objects.at(0);
                Remap(old, ids);
                if (Modified(object, old, object.id == rootId))
                    object.prefab.reset();
                else
                    removed.insert(object.id);
            }
        for (auto& object : candidate.objects)
            if (removed.contains(object.parentId))
                object.parentId = rootId;
        std::erase_if(candidate.objects,
                      [&](const auto& object)
                      {
                          return removed.contains(object.id);
                      });
    }
    void Prefab::Refresh(SceneLayout& scene, const std::filesystem::path& assetsRoot)
    {
        Refresh(scene, assetsRoot, {});
    }
    static std::map<std::string, std::string> RefreshObjectIds(const SceneLayout& candidate, const SceneLayout& source,
                                                               const std::string& rootId, const std::string& sourceRoot,
                                                               const std::filesystem::path& path)
    {
        std::map<std::string, std::string> ids;
        for (const auto& object : candidate.objects)
            if (object.prefab && object.prefab->rootId == rootId)
            {
                if (object.prefab->asset != path || !ids.emplace(object.prefab->sourceId, object.id).second)
                    throw std::runtime_error("Invalid prefab instance links");
            }
        ids[sourceRoot] = rootId;
        for (const auto& object : source.objects)
            if (!ids.contains(object.id))
                ids[object.id] = Unique(candidate, rootId + "-" + object.id);
        return ids;
    }
    void Prefab::Refresh(SceneLayout& scene, const std::filesystem::path& assetsRoot,
                         const std::map<std::filesystem::path, SceneLayout>& preparedAssets,
                         std::shared_ptr<const Engine::ContentContext> context)
    {
        if (!context)
            context = Engine::ContentContext::Capture(assetsRoot);
        context->RequireRoot(assetsRoot);
        auto candidate = scene;
        std::map<std::string, std::filesystem::path> roots;
        for (const auto& object : candidate.objects)
            if (object.prefab)
                roots.emplace(object.prefab->rootId, object.prefab->asset);
        for (const auto& [rootId, path] : roots)
        {
            auto root = std::find_if(candidate.objects.begin(), candidate.objects.end(),
                                     [&](const auto& object)
                                     {
                                         return object.id == rootId;
                                     });
            if (root == candidate.objects.end() || !root->prefab)
            {
                Unpack(candidate, rootId);
                continue;
            }
            if (!ValidPath(path))
                throw std::runtime_error("Invalid prefab path");
            RefreshScope scope(assetsRoot / path);
            auto source =
                preparedAssets.contains(path) ? preparedAssets.at(path) : SceneLayout::Load(assetsRoot / path);
            source.ResolveAssets(Engine::AssetDatabase(context, true));
            Refresh(source, assetsRoot, preparedAssets, context);
            const auto sourceRoot = Root(source);
            const auto ids = RefreshObjectIds(candidate, source, rootId, sourceRoot, path);
            const auto slots = ExternalSlots(source);
            if (root->prefab->referenceVersion == 0 && !slots.empty())
                throw PrefabReferenceMigrationRequired("Legacy prefab external references require explicit binding: " +
                                                       rootId);
            auto bindings = root->prefab->externalBindings;
            for (const auto& [slot, label] : slots)
            {
                static_cast<void>(label);
                bindings.try_emplace(slot, ScriptObjectReference{});
            }
            const auto live = MergeInstance(candidate, source, ids, rootId, path, bindings);
            RemoveDeletedSources(candidate, live, ReferenceMap(ids, bindings), rootId);
            std::erase_if(bindings,
                          [&](const auto& entry)
                          {
                              return !slots.contains(entry.first);
                          });
            Find(candidate, rootId).prefab->externalBindings = std::move(bindings);
        }
        static_cast<void>(candidate.Serialize());
        scene = std::move(candidate);
    }
    void Prefab::Bind(SceneLayout& scene, const std::string& rootId, const std::filesystem::path& path)
    {
        if (!ValidPath(path))
            throw std::runtime_error("Invalid prefab path");
        const auto [asset, bindings] = ExtractScoped(scene, rootId);
        const auto selected = Descendants(scene, rootId);
        for (const auto& source : asset.objects)
        {
            const auto found =
                std::find_if(scene.objects.begin(), scene.objects.end(),
                             [&](const auto& object)
                             {
                                 return selected.contains(object.id) &&
                                        (object.id == source.id || (object.prefab && object.prefab->rootId == rootId &&
                                                                    object.prefab->sourceId == source.id));
                             });
            if (found == scene.objects.end())
                throw std::runtime_error("Prefab bind object missing");
            found->prefab =
                PrefabLink{path, source.id, rootId, Baseline(source, asset.prefabSlots, asset.assetReferences)};
        }
        Find(scene, rootId).prefab->externalBindings = bindings;
    }
    void Prefab::Unpack(SceneLayout& scene, const std::string& rootId)
    {
        for (auto& object : scene.objects)
            if (object.prefab && object.prefab->rootId == rootId)
                object.prefab.reset();
    }
    std::vector<PrefabOverride> Prefab::Overrides(const SceneLayout& scene, const std::string& objectId)
    {
        auto copy = scene;
        const auto& object = Find(copy, objectId);
        const auto baseline = BaselineInScene(scene, object);
        std::vector<PrefabOverride> result;
        Differences(Properties(object, objectId == object.prefab->rootId),
                    Properties(baseline, objectId == object.prefab->rootId), "", result);
        return result;
    }
    void Prefab::RevertOverride(SceneLayout& scene, const std::string& objectId, const std::string& property)
    {
        auto candidate = scene;
        auto& object = Find(candidate, objectId);
        const auto differences = Overrides(candidate, objectId);
        const auto found = std::find_if(differences.begin(), differences.end(),
                                        [&](const auto& item)
                                        {
                                            return item.path == property;
                                        });
        if (found == differences.end())
            throw std::runtime_error("Prefab property is not overridden");
        auto properties = Properties(object, objectId == object.prefab->rootId);
        const Json::json_pointer pointer(property);
        if (found->source.empty())
            properties = properties.patch(Json::array({Json{{"op", "remove"}, {"path", property}}}));
        else
            properties[pointer] = Json::parse(found->source);
        SetProperties(object, properties);
        if (!object.camera && candidate.settings.mainCamera == object.id)
            candidate.settings.mainCamera.clear();
        static_cast<void>(candidate.Serialize());
        scene = std::move(candidate);
    }
    void Prefab::RevertInstance(SceneLayout& scene, const std::string& rootId, const std::filesystem::path& assetsRoot)
    {
        auto candidate = scene;
        const auto& root = Find(candidate, rootId);
        if (!root.prefab || root.prefab->rootId != rootId)
            throw std::runtime_error("Revert requires a prefab root");
        Refresh(candidate, assetsRoot);
        for (auto& object : candidate.objects)
            if (object.prefab && object.prefab->rootId == rootId)
            {
                const auto source = BaselineInScene(candidate, object);
                SetProperties(object, Properties(source, object.id == rootId));
            }
        if (!candidate.settings.mainCamera.empty() &&
            std::none_of(candidate.objects.begin(), candidate.objects.end(),
                         [&](const auto& object)
                         {
                             return object.id == candidate.settings.mainCamera && object.camera;
                         }))
            candidate.settings.mainCamera.clear();
        static_cast<void>(candidate.Serialize());
        scene = std::move(candidate);
    }
    void Prefab::EditOverride(SceneLayout& asset, const SceneLayout& scene, const std::string& objectId,
                              const std::string& property)
    {
        auto copy = scene;
        auto object = Find(copy, objectId);
        const auto overrides = Overrides(scene, objectId);
        if (std::none_of(overrides.begin(), overrides.end(),
                         [&](const auto& item)
                         {
                             return item.path == property;
                         }))
            throw std::runtime_error("Prefab property is not overridden");
        const auto sourceId = object.prefab->sourceId;
        auto& source = Find(asset, sourceId);
        std::map<std::string, std::string> ids;
        for (const auto& item : scene.objects)
            if (item.prefab && item.prefab->rootId == object.prefab->rootId)
                ids[item.id] = item.prefab->sourceId;
        std::set<std::string> reserved;
        for (const auto& item : asset.objects)
            reserved.insert(item.id);
        for (const auto& [slot, label] : asset.prefabSlots)
        {
            static_cast<void>(label);
            reserved.insert(slot);
        }
        std::map<std::string, std::string> outside, pendingSlots;
        object.VisitObjectReferences(
            [&](std::string& target)
            {
                if (target.empty() || ids.contains(target))
                    return;
                if (!outside.contains(target))
                {
                    std::string slot;
                    for (size_t i = 1;; ++i)
                    {
                        slot = "prefab-external-" + std::to_string(i);
                        if (!reserved.contains(slot))
                            break;
                    }
                    reserved.insert(slot);
                    outside[target] = slot;
                    pendingSlots[slot] = target;
                }
                target = outside.at(target);
            });
        Remap(object, ids);
        const bool root = objectId == object.prefab->rootId;
        const auto current = Properties(object, root);
        auto properties = Properties(source, root);
        const Json::json_pointer pointer(property);
        if (current.contains(pointer))
            properties[pointer] = current.at(pointer);
        else
            properties = properties.patch(Json::array({Json{{"op", "remove"}, {"path", property}}}));
        SetProperties(source, properties);
        source.VisitObjectReferences(
            [&](std::string& target)
            {
                if (pendingSlots.contains(target))
                    asset.prefabSlots[target] = pendingSlots.at(target);
            });
        asset.Validate();
    }
    void Prefab::ApplyOverride(const SceneLayout& scene, const std::string& objectId, const std::string& property,
                               const std::filesystem::path& assetsRoot)
    {
        const auto& object = Find(scene, objectId);
        if (!object.prefab)
            throw std::runtime_error("Object is not a prefab instance");
        const auto path = object.prefab->asset;
        auto asset = SceneLayout::Load(assetsRoot / path, assetsRoot);
        Refresh(asset, assetsRoot);
        EditOverride(asset, scene, objectId, property);
        asset.Save(assetsRoot / path);
    }
    SceneLayout Prefab::Variant(const SceneLayout& scene, const std::string& rootId)
    {
        auto copy = scene;
        const auto& root = Find(copy, rootId);
        if (!root.prefab || root.prefab->rootId != rootId)
            throw std::runtime_error("Variant requires a prefab root");
        auto [variant, connections] = ExtractScoped(scene, rootId);
        std::map<std::string, std::string> coordinates;
        for (const auto& object : scene.objects)
            if (object.prefab && object.prefab->rootId == rootId)
                coordinates[object.id] = object.prefab->sourceId;
        for (const auto& [slot, target] : connections)
            if (!target.id.empty())
                coordinates.try_emplace(target.id, slot);
        for (auto& source : variant.objects)
        {
            const auto found = std::find_if(scene.objects.begin(), scene.objects.end(),
                                            [&](const auto& item)
                                            {
                                                return item.prefab && item.prefab->rootId == rootId &&
                                                       item.prefab->sourceId == source.id;
                                            });
            if (found != scene.objects.end())
            {
                source.prefab = found->prefab;
                source.prefab->rootId = root.prefab->sourceId;
                for (auto& [slot, target] : source.prefab->externalBindings)
                {
                    static_cast<void>(slot);
                    if (coordinates.contains(target.id))
                        target.id = coordinates.at(target.id);
                }
            }
        }
        static_cast<void>(variant.Serialize());
        return variant;
    }
}
