#include <Engine/Core/Profiler.h>
#include <SceneRuntime/Scene/SceneLayout.h>
#include <iterator>
#include "SceneComponentJson.h"
#include "SceneSettingsJson.h"
#include <SceneRuntime/Scene/Prefab.h>
#include <Engine/Assets/AssetPath.h>
#include <Engine/Core/Json.h>
#include <Engine/Core/Utf8.h>
#include <cmath>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <unordered_map>
#include <optional>
#include <utility>

namespace
{
    using Engine::Json;
    using Engine::JsonNumber;
    using Engine::JsonArray;
    using Engine::JsonObject;

    void ValidateCamera(const SceneRuntime::SceneLayout& layout)
    {
        if (layout.settings.mainCamera.empty())
            return;
        if (std::none_of(layout.objects.begin(), layout.objects.end(),
                         [&](const auto& object)
                         {
                             return object.id == layout.settings.mainCamera && object.camera;
                         }))
            throw std::runtime_error("Main camera must reference an object with Camera");
    }

    void ValidateParents(const std::vector<SceneRuntime::ScenePlacement>& objects)
    {
        std::unordered_map<std::string, size_t> indices;
        indices.reserve(objects.size());
        for (size_t index = 0; index < objects.size(); ++index)
            indices.emplace(objects[index].id, index);
        std::vector<std::optional<size_t>> parents(objects.size());
        for (size_t index = 0; index < objects.size(); ++index)
        {
            const auto& object = objects[index];
            if (object.parentId.empty())
                continue;
            const auto found = indices.find(object.parentId);
            if (found == indices.end())
                throw std::runtime_error(object.id + ": missing parent " + object.parentId);
            if (found->second == index)
                throw std::runtime_error(object.id + ": object cannot parent itself");
            parents[index] = found->second;
        }
        std::vector<unsigned char> visited(objects.size(), 0);
        std::vector<size_t> chain;
        chain.reserve(objects.size());
        for (size_t index = 0; index < objects.size(); ++index)
        {
            chain.clear();
            std::optional<size_t> current = index;
            while (current && visited[*current] == 0)
            {
                visited[*current] = 1;
                chain.push_back(*current);
                current = parents[*current];
            }
            if (current && visited[*current] == 1)
                throw std::runtime_error(objects[*current].id + ": parent cycle detected");
            for (const auto item : chain)
                visited[item] = 2;
        }
    }

    std::array<float, 3> ReadVector(const Json& object, const char* key, bool scale = false)
    {
        const auto& values = JsonArray(object.at(key));
        if (values.size() != 3)
            throw std::runtime_error("Transform requires three components");
        std::array<float, 3> result;
        for (uint32_t i = 0; i < 3; ++i)
        {
            const double value = JsonNumber(values.at(i));
            if (!std::isfinite(value) || std::abs(value) > (std::numeric_limits<float>::max)() ||
                (scale && std::abs(value) < static_cast<double>(0.000001f)))
                throw std::runtime_error("Transform contains an invalid number or zero scale");
            result[i] = static_cast<float>(value);
        }
        return result;
    }
}

namespace SceneRuntime
{
    static bool ValidPrefabLink(const PrefabLink& link)
    {
        return Prefab::ValidPath(link.asset) && !link.sourceId.empty() && !link.rootId.empty() &&
               !link.baseline.empty() && link.baseline.size() <= 1048576 && link.referenceVersion <= 1 &&
               link.externalBindings.size() <= 256 && (link.referenceVersion != 0 || link.externalBindings.empty()) &&
               std::all_of(link.externalBindings.begin(), link.externalBindings.end(),
                           [](const auto& entry)
                           {
                               return !entry.first.empty() && entry.first.size() <= 128 &&
                                      entry.first.find('\0') == std::string::npos && entry.second.id.size() <= 128 &&
                                      entry.second.id.find('\0') == std::string::npos;
                           });
    }
    static void ReadPrefabLink(const Json& object, ScenePlacement& placement)
    {
        if (object.contains("prefab"))
        {
            const auto& prefabValue = JsonObject(object.at("prefab"));
            const auto asset = prefabValue.at("asset").get<std::string>();
            PrefabLink link;
            link.asset = std::filesystem::path(std::u8string(asset.begin(), asset.end()));
            link.sourceId = prefabValue.at("source").get<std::string>();
            link.rootId = prefabValue.at("root").get<std::string>();
            link.baseline = prefabValue.at("baseline").get<std::string>();
            if (prefabValue.contains("referenceVersion") &&
                (!prefabValue.at("referenceVersion").is_number_integer() ||
                 (prefabValue.at("referenceVersion") != 0 && prefabValue.at("referenceVersion") != 1)))
                throw std::runtime_error("Unsupported prefab reference version");
            link.referenceVersion = prefabValue.value("referenceVersion", 0u);
            if (prefabValue.contains("externalBindings"))
                for (const auto& [slot, target] : JsonObject(prefabValue.at("externalBindings")).items())
                    link.externalBindings[slot] = {target.get<std::string>()};
            if (!ValidPrefabLink(link))
                throw std::runtime_error("Invalid prefab link");
            placement.prefab = std::move(link);
        }
    }

    static ScenePlacement ReadPlacement(const Json& value, double version, std::unordered_set<std::string>& ids)
    {
        const auto& object = JsonObject(value);
        ScenePlacement placement;
        placement.id = object.at("id").get<std::string>();
        if (placement.id.empty() || !ids.insert(placement.id).second)
            throw std::runtime_error("Empty or duplicate object ID: " + placement.id);
        try
        {
            placement.name = object.at("name").get<std::string>();
            if (object.contains("persistent"))
                placement.persistent = object.at("persistent").get<bool>();
            if (object.contains("parent"))
                placement.parentId = object.at("parent").get<std::string>();
            ReadSceneComponents(object, placement, version == 2.0);
            ReadPrefabLink(object, placement);
            placement.position = ReadVector(object, "position");
            placement.rotation = ReadVector(object, "rotation");
            placement.scale = ReadVector(object, "scale", true);
        }
        catch (const std::exception& error)
        {
            throw std::runtime_error(placement.id + ": " + error.what());
        }
        return placement;
    }

    static void ValidateSceneCounts(const SceneLayout& layout)
    {
        const auto menus = std::count_if(layout.objects.begin(), layout.objects.end(),
                                         [](const auto& object)
                                         {
                                             return object.canvas && object.canvas->enabled && object.canvas->menu;
                                         });
        if (menus > 1)
            throw std::runtime_error("At most one enabled menu Canvas is supported");
        size_t localLights = 0;
        for (const auto& object : layout.objects)
        {
            if (object.pointLight && object.pointLight->enabled)
                ++localLights;
            if (object.spotLight && object.spotLight->enabled)
                ++localLights;
        }
        if (localLights > 32)
            throw std::runtime_error("At most 32 enabled Point/Spot lights are supported");
    }
    static void ValidateSceneOwnership(const SceneLayout& layout)
    {
        std::set<std::string> membership;
        for (const auto& [name, ids] : layout.sceneObjects)
        {
            if (name.empty() || name.size() > 4096 || name.find('\0') != std::string::npos)
                throw std::runtime_error("Invalid scene ownership name");
            const auto duplicate = std::find_if(ids.begin(), ids.end(),
                                                [&](const auto& id)
                                                {
                                                    return !membership.insert(id).second;
                                                });
            if (duplicate != ids.end())
                throw std::runtime_error("Duplicate scene ownership object: " + *duplicate);
        }
    }
    static void ValidateLayoutContents(const SceneLayout& layout)
    {
        ValidateSceneOwnership(layout);
        if (std::any_of(layout.objects.begin(), layout.objects.end(),
                        [](const auto& p)
                        {
                            return (p.terrain && p.tilemap) || ((p.terrain || p.tilemap) && p.meshRenderer);
                        }))
            throw std::runtime_error("Procedural renderer cannot share a MeshRenderer or other procedural renderer");
        ValidateParents(layout.objects);
        if (std::any_of(std::begin(layout.objects), std::end(layout.objects),
                        [](const auto& object)
                        {
                            return object.ragdoll && !object.animator;
                        }))
            throw std::runtime_error("Ragdoll requires an Animator");
        ValidateCamera(layout);
        ValidateSceneCounts(layout);
    }
    static void ValidatePrefabSlots(const SceneLayout& layout, const std::unordered_set<std::string>& ids,
                                    double version)
    {
        if ((version < 5.0 && !layout.prefabSlots.empty()) || layout.prefabSlots.size() > 256)
            throw std::runtime_error("Too many prefab external slots");
        for (const auto& [slot, label] : layout.prefabSlots)
            if (slot.empty() || slot.size() > 128 || ids.contains(slot) || label.size() > 4096 ||
                slot.find('\0') != std::string::npos || label.find('\0') != std::string::npos)
                throw std::runtime_error("Invalid or ambiguous prefab external slot");
    }
    static void ValidateAssetMetadata(const SceneLayout& layout)
    {
        for (const auto& [path, id] : layout.assetReferences)
            if (!Engine::AssetPath::Valid(path) || !Engine::AssetPath::ValidId(id))
                throw std::runtime_error("Invalid asset reference metadata");
    }
    static SceneLayout ParseDocument(const Json& document)
    {
        try
        {
            JsonObject(document);
            const double version = JsonNumber(document.at("version"));
            if (version != 2.0 && version != 3.0 && version != 4.0 && version != 5.0)
                throw std::runtime_error("Unsupported layout version");
            SceneLayout layout;
            if (document.contains("sceneObjects"))
                layout.sceneObjects =
                    document.at("sceneObjects").get<std::map<std::string, std::vector<std::string>>>();
            if (document.contains("assetReferences"))
                layout.assetReferences = document.at("assetReferences").get<std::map<std::string, std::string>>();
            ValidateAssetMetadata(layout);
            if (document.contains("prefabSlots"))
                layout.prefabSlots = document.at("prefabSlots").get<std::map<std::string, std::string>>();
            if (version >= 4.0)
                layout.settings = ReadSceneSettings(JsonObject(document.at("settings")));
            if (document.contains("transformSpace"))
                throw std::runtime_error("Scenes always use local transforms");
            std::unordered_set<std::string> ids;
            for (const auto& value : JsonArray(document.at("objects")))
            {
                auto placement = ReadPlacement(value, version, ids);
                if (version < 5.0 && placement.prefab && placement.prefab->referenceVersion != 0)
                    throw std::runtime_error("Prefab reference scope requires layout version 5");
                layout.objects.push_back(std::move(placement));
            }
            ValidatePrefabSlots(layout, ids, version);
            ValidateLayoutContents(layout);
            return layout;
        }
        catch (const Json::exception& error)
        {
            throw std::runtime_error("Invalid layout JSON: " + std::string(error.what()));
        }
    }

    SceneLayout SceneLayout::Parse(std::string_view json)
    {
        try
        {
            return ParseDocument(Json::parse(json));
        }
        catch (const Json::exception& error)
        {
            throw std::runtime_error("Invalid layout JSON: " + std::string(error.what()));
        }
    }

    static unsigned int LayoutVersion(const SceneLayout& layout)
    {
        return !layout.prefabSlots.empty() ||
                       std::any_of(layout.objects.begin(), layout.objects.end(),
                                   [](const auto& object)
                                   {
                                       return object.prefab && object.prefab->referenceVersion != 0;
                                   })
                   ? 5
                   : 4;
    }
    static Json WritePrefabLink(const PrefabLink& link)
    {
        Json value{
            {"asset", link.asset}, {"source", link.sourceId}, {"root", link.rootId}, {"baseline", link.baseline}};
        if (link.referenceVersion != 0)
        {
            value["referenceVersion"] = link.referenceVersion;
            value["externalBindings"] = Json::object();
            for (const auto& [slot, target] : link.externalBindings)
                value["externalBindings"][slot] = target.id;
        }
        return value;
    }
    static Json LayoutDocument(const SceneLayout& layout)
    {
        Json document = {{"version", LayoutVersion(layout)},
                         {"settings", WriteSceneSettings(layout.settings)},
                         {"objects", Json::array()},
                         {"assetReferences", layout.assetReferences},
                         {"sceneObjects", layout.sceneObjects}};
        if (!layout.prefabSlots.empty())
            document["prefabSlots"] = layout.prefabSlots;
        for (const auto& p : layout.objects)
        {
            Json object = {{"id", p.id},
                           {"name", p.name},
                           {"parent", p.parentId},
                           {"components", WriteSceneComponents(p)},
                           {"position", p.position},
                           {"rotation", p.rotation},
                           {"scale", p.scale}};
            if (p.persistent)
                object["persistent"] = true;
            if (p.prefab)
                object["prefab"] = WritePrefabLink(*p.prefab);
            for (const auto& vector : {p.position, p.rotation, p.scale})
                if (std::any_of(vector.begin(), vector.end(),
                                [](float value)
                                {
                                    return !std::isfinite(value);
                                }))
                    throw std::runtime_error("Cannot save nonfinite transform");
            document["objects"].push_back(std::move(object));
        }
        return document;
    }

    static void ValidateDocumentValues(const Json& value)
    {
        if (value.is_number_float() && !std::isfinite(value.get<double>()))
            throw std::runtime_error("Cannot normalize nonfinite component number");
        if (value.is_string() && !Engine::ValidUtf8(value.get_ref<const Json::string_t&>()))
            throw std::runtime_error("Scene strings must be valid UTF-8");
        if (value.is_object())
            for (const auto& [key, child] : value.items())
            {
                if (!Engine::ValidUtf8(key))
                    throw std::runtime_error("Scene keys must be valid UTF-8");
                ValidateDocumentValues(child);
            }
        else if (value.is_array())
            for (const auto& child : value)
                ValidateDocumentValues(child);
    }
    SceneLayout SceneLayout::Normalized() const
    {
        const auto document = LayoutDocument(*this);
        // 保存形式のUTF-8と数値の制約を、文字列の生成・再解析なしで維持します。
        ValidateDocumentValues(document);
        return ParseDocument(document);
    }
    void SceneLayout::Validate() const
    {
        Engine::CpuScope timing("Scene layout validation");
        static_cast<void>(Normalized());
    }

    static bool ReusableMaterialComponents(const ScenePlacement& components)
    {
        if (!components.material)
            return false;
        size_t count = 0;
        components.VisitComponents(
            [&](const auto&)
            {
                ++count;
            });
        if (count != 1)
            return false;
        bool canonical = true;
        auto material = *components.material;
        ComponentAssetReferences(
            material,
            [&](const std::filesystem::path& path)
            {
                canonical = canonical && path.native() == path.generic_string<std::filesystem::path::value_type>();
            });
        return canonical;
    }
    static void ValidateComponentChanges(const ScenePlacement& object, const ScenePlacement& before,
                                         std::vector<ScenePlacement>& validatedComponents)
    {
        ScenePlacement changed;
        bool structureChanged = false;
#define ENGINE_SCENE_CHANGED_OPTIONAL(Type, member, references, assets, order)                                         \
    if (object.member != before.member)                                                                                \
    {                                                                                                                  \
        structureChanged |= !object.member || !before.member || object.member->id != before.member->id;                \
        changed.member = object.member;                                                                                \
    }
#define ENGINE_SCENE_CHANGED_REPEATED(Type, member, references, assets, order)                                         \
    structureChanged |= object.member != before.member;
        ENGINE_SCENE_COMPONENTS(ENGINE_SCENE_CHANGED_OPTIONAL, ENGINE_SCENE_CHANGED_REPEATED)
#undef ENGINE_SCENE_CHANGED_OPTIONAL
#undef ENGINE_SCENE_CHANGED_REPEATED
        // 構造が同じなら未変更の値は検証済みです。ID・多重度が変わる場合は従来の全体検証を維持します。
        const auto& components = structureChanged ? object : changed;
        // パスの成分比較で生表記の違いを隠さないよう、標準表記の単独Materialだけを共有します。
        const bool reusable = ReusableMaterialComponents(components);
        if (reusable && std::any_of(validatedComponents.begin(), validatedComponents.end(),
                                    [&](const auto& validated)
                                    {
                                        return validated.SameComponents(components);
                                    }))
        {
            Engine::CpuProfiler::Add("Component validation cache hit", 0);
            return;
        }
        {
            Engine::CpuScope timing("Component change validation");
            ScenePlacement checked;
            ReadSceneComponents(Json{{"components", WriteSceneComponents(components)}}, checked, false);
        }
        // この呼び出しで成功した完全な成分集合だけを共有し、次回の検証へ状態を持ち越しません。
        if (reusable && validatedComponents.size() < 8)
        {
            ScenePlacement validated;
            validated.CopyComponents(components);
            validatedComponents.push_back(std::move(validated));
        }
    }

    static void ValidateObjectChanges(const ScenePlacement& object, const ScenePlacement& before,
                                      std::vector<ScenePlacement>& validatedComponents)
    {
        for (const auto& vector : {object.position, object.rotation, object.scale})
            if (std::any_of(vector.begin(), vector.end(),
                            [](float value)
                            {
                                return !std::isfinite(value);
                            }))
                throw std::runtime_error("Cannot save nonfinite transform");
        if (std::any_of(object.scale.begin(), object.scale.end(),
                        [](float value)
                        {
                            return std::abs(value) < 0.000001f;
                        }))
            throw std::runtime_error("Transform contains an invalid number or zero scale");
        if (!object.SameComponents(before))
        {
            ValidateComponentChanges(object, before, validatedComponents);
        }
        if (object.prefab != before.prefab && object.prefab && (!ValidPrefabLink(*object.prefab)))
            throw std::runtime_error("Invalid prefab link");
        if ((object.terrain && object.tilemap) || ((object.terrain || object.tilemap) && object.meshRenderer))
            throw std::runtime_error("Procedural renderer cannot share a MeshRenderer or other procedural renderer");
        if (object.ragdoll && !object.animator)
            throw std::runtime_error("Ragdoll requires an Animator");
    }
    static void ValidateChangedAssetReferences(const SceneLayout& layout, const SceneLayout& validatedBefore)
    {
        if (layout.assetReferences != validatedBefore.assetReferences)
            for (const auto& [path, id] : layout.assetReferences)
                if (!Engine::AssetPath::Valid(path) || !Engine::AssetPath::ValidId(id))
                    throw std::runtime_error("Invalid asset reference metadata");
    }
    static void CountSceneComponents(const ScenePlacement& object, size_t& menus, size_t& localLights)
    {
        if (object.canvas && object.canvas->enabled && object.canvas->menu)
            ++menus;
        if (object.pointLight && object.pointLight->enabled)
            ++localLights;
        if (object.spotLight && object.spotLight->enabled)
            ++localLights;
    }
    void SceneLayout::ValidateChanges(const SceneLayout& validatedBefore) const
    {
        if (prefabSlots != validatedBefore.prefabSlots || objects.size() != validatedBefore.objects.size())
        {
            Validate();
            return;
        }
        for (size_t index = 0; index < objects.size(); ++index)
            if (objects[index].id != validatedBefore.objects[index].id)
            {
                Validate();
                return;
            }
        try
        {
            if (settings != validatedBefore.settings)
                static_cast<void>(ReadSceneSettings(WriteSceneSettings(settings)));
            ValidateChangedAssetReferences(*this, validatedBefore);
            if (sceneObjects != validatedBefore.sceneObjects)
                ValidateSceneOwnership(*this);
            // オブジェクト ID は検証済みの基準状態と一致しているため、階層が無効になる可能性があるのは親の編集時だけです。
            bool parentsChanged = false;
            size_t menus = 0, localLights = 0;
            std::vector<ScenePlacement> validatedComponents;
            for (size_t index = 0; index < objects.size(); ++index)
            {
                const auto& object = objects[index];
                const auto& before = validatedBefore.objects[index];
                parentsChanged = parentsChanged || object.parentId != before.parentId;
                ValidateObjectChanges(object, before, validatedComponents);
                CountSceneComponents(object, menus, localLights);
            }
            if (parentsChanged)
                ValidateParents(objects);
            ValidateCamera(*this);
            if (menus > 1)
                throw std::runtime_error("At most one enabled menu Canvas is supported");
            if (localLights > 32)
                throw std::runtime_error("At most 32 enabled Point/Spot lights are supported");
        }
        catch (const Json::exception& error)
        {
            throw std::runtime_error("Invalid layout JSON: " + std::string(error.what()));
        }
    }

    std::string SceneLayout::Serialize() const
    {
        const auto settingsObject = WriteSceneSettings(settings);
        std::string json = "{\n  \"version\": " + std::to_string(LayoutVersion(*this)) +
                           ",\n  \"settings\": " + settingsObject.dump() + ",\n  \"objects\": [\n";
        for (size_t i = 0; i < objects.size(); ++i)
        {
            const auto& p = objects[i];
            Json object = {{"id", p.id},
                           {"name", p.name},
                           {"parent", p.parentId},
                           {"components", WriteSceneComponents(p)},
                           {"position", p.position},
                           {"rotation", p.rotation},
                           {"scale", p.scale}};
            if (p.persistent)
                object["persistent"] = true;
            if (p.prefab)
                object["prefab"] = WritePrefabLink(*p.prefab);
            for (const auto& vector : {p.position, p.rotation, p.scale})
                if (std::any_of(vector.begin(), vector.end(),
                                [](float value)
                                {
                                    return !std::isfinite(value);
                                }))
                    throw std::runtime_error("Cannot save nonfinite transform");
            json += "    " + object.dump() + (i + 1 == objects.size() ? "\n" : ",\n");
        }
        json += "  ],\n  \"assetReferences\": " + Json(assetReferences).dump() +
                ",\n  \"sceneObjects\": " + Json(sceneObjects).dump() + "\n}\n";
        if (!prefabSlots.empty())
        {
            auto document = Json::parse(json);
            document["prefabSlots"] = prefabSlots;
            json = document.dump(2) + "\n";
        }
        static_cast<void>(Parse(json));
        return json;
    }

}

namespace SceneRuntime
{
    auto ScenePlacement::HasComponentId(const std::string& componentId) const -> bool
    {
        bool found = componentId == "transform";
        VisitComponents(
            [&](const auto& component)
            {
                found = found || component.id == componentId;
            });
        return found;
    }

    auto ScenePlacement::SameComponents(const ScenePlacement& other) const -> bool
    {
#define ENGINE_SCENE_COMPARE(Type, member, references, assets, order)                                                  \
    if (!(member == other.member))                                                                                     \
        return false;
        ENGINE_SCENE_COMPONENTS(ENGINE_SCENE_COMPARE, ENGINE_SCENE_COMPARE)
#undef ENGINE_SCENE_COMPARE
        return persistent == other.persistent;
    }

    auto ScenePlacement::CopyComponents(const ScenePlacement& other) -> void
    {
        persistent = other.persistent;
#define ENGINE_SCENE_COPY(Type, member, references, assets, order) member = other.member;
        ENGINE_SCENE_COMPONENTS(ENGINE_SCENE_COPY, ENGINE_SCENE_COPY)
#undef ENGINE_SCENE_COPY
    }

    auto ScenePlacement::SetModel(std::filesystem::path path) -> void
    {
        if (!meshRenderer)
            meshRenderer.emplace();
        meshRenderer->model = std::move(path);
    }
}
