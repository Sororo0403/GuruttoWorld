#include "PrefabMerge.h"

namespace SceneRuntime::PrefabMerge
{
    bool HasScriptIdentity(const Json& item)
    {
        if (!item.is_object() || item.value("kind", "") != "object" || !item.contains("value") ||
            !item.at("value").contains("id"))
            return false;
        const auto& id = item.at("value").at("id");
        return id.is_object() && (id.value("kind", "") == "string" || id.value("kind", "") == "number");
    }

    ArrayPolicy Policy(const std::string& type, const std::string& path, const Json& old)
    {
        static const std::map<std::pair<std::string, std::string>, std::string> identities{
            {{"Animator", "/states"}, "name"},    {{"Animator", "/blendTrees"}, "name"},
            {{"Animator", "/ik"}, "name"},        {{"AudioMixer", "/groups"}, "name"},
            {{"Ragdoll", "/bones"}, "bone"},      {{"Canvas", "/menu/entries"}, "index"},
            {{"Canvas", "/menu/settings"}, "key"}};
        const auto found = identities.find({type, path});
        if (found != identities.end())
            return {ArrayMode::Identity, found->second};
        if (type == "Script" && path.ends_with("/value"))
        {
            const bool identity = std::any_of(old.begin(), old.end(), HasScriptIdentity);
            return {identity ? ArrayMode::Identity : ArrayMode::Index, identity ? "$scriptId" : ""};
        }
        static const std::set<std::string> indexed{"/center",
                                                   "/size",
                                                   "/velocity",
                                                   "/angularVelocity",
                                                   "/anchor",
                                                   "/connectedAnchor",
                                                   "/axis",
                                                   "/color",
                                                   "/shaderParameters",
                                                   "/hoverColor",
                                                   "/pressedColor",
                                                   "/referenceSize",
                                                   "/anchorMin",
                                                   "/anchorMax",
                                                   "/pivot",
                                                   "/position",
                                                   "/rotation",
                                                   "/scale",
                                                   "/uv",
                                                   "/target",
                                                   "/hint",
                                                   "/destination",
                                                   "/bindOffset"};
        const auto separator = path.find_last_of('/');
        const auto field = separator == std::string::npos ? path : path.substr(separator);
        static const std::set<std::pair<std::string, std::string>> indexedPaths{
            {"Terrain", "/heights"},  {"NavMesh", "/heights"}, {"Tilemap", "/tiles"},
            {"NavMesh", "/walkable"}, {"Material", "/slots"},  {"MeshRenderer", "/lods"}};
        if (indexed.contains(field) || indexedPaths.contains({type, path}))
            return {ArrayMode::Index, ""};
        return {};
    }

    std::string Escape(const std::string& name)
    {
        std::string result;
        for (const char c : name)
            result += c == '~' ? "~0" : c == '/' ? "~1" : std::string(1, c);
        return result;
    }

    std::string Identity(const Json& item, const ArrayPolicy& policy, const std::string& path)
    {
        if (!item.is_object())
            Conflict(path, "identity requires objects");
        Json id;
        if (policy.identity == "$scriptId")
        {
            if (item.value("kind", "") != "object" || !item.contains("value") || !item.at("value").contains("id"))
                Conflict(path, "missing Script object id");
            const auto& value = item.at("value").at("id");
            if (!value.is_object() || (value.value("kind", "") != "string" && value.value("kind", "") != "number"))
                Conflict(path, "Script object id must be a string or number");
            id = value;
        }
        else
        {
            if (!item.contains(policy.identity))
                Conflict(path, "missing identity " + policy.identity);
            id = item.at(policy.identity);
            if (!id.is_string() && !id.is_number_integer())
                Conflict(path, "invalid identity type");
        }
        return id.dump();
    }

    std::map<std::string, Json> Items(const Json& array, const ArrayPolicy& policy, const std::string& path)
    {
        std::map<std::string, Json> result;
        for (const auto& item : array)
            if (!result.emplace(Identity(item, policy, path), item).second)
                Conflict(path, "duplicate identity");
        return result;
    }

    std::map<std::string, Json> MergeItems(const std::map<std::string, Json>& before,
                                           const std::map<std::string, Json>& local,
                                           const std::map<std::string, Json>& remote, const std::string& path,
                                           const std::string& type)
    {
        std::map<std::string, Json> merged;
        for (const auto& [id, value] : before)
        {
            if (!local.contains(id) || !remote.contains(id))
            {
                if ((local.contains(id) && local.at(id) != value) || (remote.contains(id) && remote.at(id) != value))
                    Conflict(path + "/" + id, "deletion conflicts with edits");
                continue;
            }
            merged[id] = Merge(value, local.at(id), remote.at(id), path + "/" + Escape(id), type);
        }
        for (const auto& [id, value] : remote)
            if (!before.contains(id))
                merged[id] = value;
        for (const auto& [id, value] : local)
            if (!before.contains(id))
            {
                if (merged.contains(id) && merged.at(id) != value)
                    Conflict(path, "different additions have the same identity");
                merged[id] = value;
            }
        return merged;
    }

    std::vector<std::string> MergeOrder(const Json& old, const Json& current, const Json& source,
                                        const std::map<std::string, Json>& before,
                                        const std::map<std::string, Json>& merged, const std::string& path,
                                        const ArrayPolicy& policy)
    {
        const auto order = [&](const Json& array, bool originalOnly)
        {
            std::vector<std::string> result;
            for (const auto& item : array)
            {
                const auto id = Identity(item, policy, path);
                if (merged.contains(id) && (!originalOnly || before.contains(id)))
                    result.push_back(id);
            }
            return result;
        };
        const auto baseOrder = order(old, true), localOrder = order(current, true), remoteOrder = order(source, true);
        const bool localMoved = localOrder != baseOrder, remoteMoved = remoteOrder != baseOrder;
        if (localMoved && remoteMoved && localOrder != remoteOrder)
            Conflict(path, "incompatible reordering");
        auto sequence = order(localMoved ? current : source, false);
        const auto& extra = localMoved ? source : current;
        size_t insertion = 0;
        for (const auto& item : extra)
        {
            const auto id = Identity(item, policy, path);
            if (!merged.contains(id))
                continue;
            const auto found = std::find(sequence.begin(), sequence.end(), id);
            if (found != sequence.end())
                insertion = static_cast<size_t>(found - sequence.begin()) + 1;
            else
                sequence.insert(sequence.begin() + insertion++, id);
        }
        return sequence;
    }

    Json MergeIdentity(const Json& old, const Json& current, const Json& source, const std::string& path,
                       const std::string& type, const ArrayPolicy& policy)
    {
        const auto before = Items(old, policy, path), local = Items(current, policy, path),
                   remote = Items(source, policy, path);
        auto merged = MergeItems(before, local, remote, path, type);
        const auto sequence = MergeOrder(old, current, source, before, merged, path, policy);
        auto result = Json::array();
        std::transform(sequence.begin(), sequence.end(), std::back_inserter(result),
                       [&](const auto& id)
                       {
                           return merged.at(id);
                       });
        return result;
    }

    void ValidateGridMerge(const Json& old, const Json& current, const Json& source, const std::string& path,
                           const std::string& type)
    {
        if (type == "Terrain" || type == "Tilemap" || type == "NavMesh")
        {
            const auto dimensionsChanged = [&](const Json& object)
            {
                return old.at("columns") != object.at("columns") || old.at("rows") != object.at("rows");
            };
            if (dimensionsChanged(current) || dimensionsChanged(source))
                for (const auto* field : {"heights", "walkable", "tiles"})
                    if (old.contains(field) && ((dimensionsChanged(source) && old.at(field) != current.at(field)) ||
                                                (dimensionsChanged(current) && old.at(field) != source.at(field))))
                        Conflict(path + "/" + field, "grid dimensions changed with cell edits");
        }
    }

    void ValidateMergeTypes(const Json& old, const Json& current, const Json& source, std::string& path,
                            std::string& type)
    {
        if (old.contains("kind") && old.at("kind").is_string() && current.contains("kind") && source.contains("kind") &&
            (old.at("kind") != current.at("kind") || old.at("kind") != source.at("kind")))
            Conflict(path, "data type changed with overrides");
        if (old.contains("type") && old.at("type").is_string() && current.contains("type") && source.contains("type"))
        {
            if (old.at("type") != current.at("type") || old.at("type") != source.at("type"))
                Conflict(path, "component type changed with overrides");
            type = old.at("type").get<std::string>();
            path.clear();
            ValidateGridMerge(old, current, source, path, type);
        }
    }

    Json MergeArray(const Json& old, const Json& current, const Json& source, const std::string& path,
                    const std::string& type)
    {
        const auto policy = Policy(type, path, old.empty() ? current : old);
        if (policy.mode == ArrayMode::Identity)
            return MergeIdentity(old, current, source, path, type, policy);
        if (policy.mode != ArrayMode::Index)
            Conflict(path, "both sides changed an atomic array");
        if (current.size() != old.size() || source.size() != old.size())
            Conflict(path, "array size changed with other edits");
        auto result = source;
        for (size_t i = 0; i < old.size(); ++i)
            result[i] = Merge(old[i], current[i], source[i], path + "/" + std::to_string(i), type);
        return result;
    }

    Json MergeObject(const Json& old, const Json& current, const Json& source, const std::string& path,
                     const std::string& type)
    {
        auto result = source;
        for (const auto& [key, value] : current.items())
        {
            const auto child = path + "/" + Escape(key);
            if (!old.contains(key))
            {
                if (source.contains(key) && source.at(key) != value)
                    Conflict(child, "different additions");
                result[key] = value;
            }
            else if (!source.contains(key))
            {
                if (value != old.at(key))
                    Conflict(child, "source deletion conflicts with edits");
            }
            else
                result[key] = Merge(old.at(key), value, source.at(key), child, type);
        }
        for (const auto& [key, value] : old.items())
            if (!current.contains(key))
            {
                if (source.contains(key) && source.at(key) != value)
                    Conflict(path + "/" + Escape(key), "local deletion conflicts with edits");
                result.erase(key);
            }
        return result;
    }

    Json Merge(const Json& old, const Json& current, const Json& source, std::string path, std::string type)
    {
        if (current == old)
            return source;
        if (source == old || current == source)
            return current;
        if (old.is_array() && current.is_array() && source.is_array())
            return MergeArray(old, current, source, path, type);
        if (!old.is_object() || !current.is_object() || !source.is_object())
            return current;
        ValidateMergeTypes(old, current, source, path, type);
        return MergeObject(old, current, source, path, type);
    }
}
