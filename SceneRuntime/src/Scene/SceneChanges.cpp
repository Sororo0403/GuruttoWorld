#include <SceneRuntime/Scene/SceneChanges.h>
#include <SceneRuntime/Scene/WorldReadView.h>
#include <SceneRuntime/Scene/SceneTransforms.h>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <stdexcept>

namespace
{
    template <class T> void MixHash(size_t& hash, const T& value)
    {
        hash ^= std::hash<T>{}(value) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
    }

    size_t ChangeHash(const SceneRuntime::ObjectChange& value)
    {
        size_t hash = 0;
        MixHash(hash, value.objectId);
        MixHash(hash, static_cast<unsigned int>(value.kind));
        return hash;
    }

    size_t ChangeHash(const SceneRuntime::ComponentChange& value)
    {
        size_t hash = 0;
        MixHash(hash, value.objectId);
        MixHash(hash, value.componentId);
        MixHash(hash, value.componentType);
        MixHash(hash, static_cast<unsigned int>(value.kind));
        return hash;
    }

    size_t ChangeHash(const SceneRuntime::SceneFieldChange& value)
    {
        size_t hash = 0;
        MixHash(hash, value.objectId);
        MixHash(hash, value.componentId);
        MixHash(hash, value.field);
        return hash;
    }

    size_t ChangeHash(const SceneRuntime::ResourceDependencyChange& value)
    {
        size_t hash = 0;
        MixHash(hash, value.objectId);
        MixHash(hash, value.componentId);
        MixHash(hash, value.before.size());
        for (const auto& path : value.before)
            MixHash(hash, path);
        for (const auto& path : value.after)
            MixHash(hash, path);
        return hash;
    }

    template <class T> void MergeUnique(std::vector<T>& target, const std::vector<T>& source)
    {
        if (source.empty())
            return;
        // 先に容量を確保し、索引が参照する要素を追加中に移動させません。
        target.reserve(target.size() + source.size());
        const auto hash = [](const T* value)
        {
            return ChangeHash(*value);
        };
        const auto equal = [](const T* left, const T* right)
        {
            return *left == *right;
        };
        std::unordered_set<const T*, decltype(hash), decltype(equal)> indexed(0, hash, equal);
        indexed.reserve(target.size() + source.size());
        for (const auto& value : target)
            indexed.insert(&value);
        for (const auto& value : source)
            if (!indexed.contains(&value))
            {
                target.push_back(value);
                indexed.insert(&target.back());
            }
    }

    template <bool Assets, class T> std::vector<std::filesystem::path> AssetsOf(const T* component)
    {
        std::vector<std::filesystem::path> paths;
        if constexpr (Assets)
        {
            if (component)
            {
                auto copy = *component;
                SceneRuntime::ComponentAssetReferences(copy,
                                                       [&](const auto& path)
                                                       {
                                                           if (!path.empty())
                                                               paths.emplace_back(path);
                                                       });
                std::sort(paths.begin(), paths.end());
                paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
            }
        }
        return paths;
    }

    template <bool Assets, class T>
    void CompareComponent(SceneRuntime::SceneChangeSet& changes, const std::string& owner, const char* type,
                          const T* before, const T* after)
    {
        using namespace SceneRuntime;
        if (before && after && *before == *after)
            return;
        if (before && after && before->id != after->id)
        {
            CompareComponent<Assets>(changes, owner, type, before, static_cast<const T*>(nullptr));
            CompareComponent<Assets>(changes, owner, type, static_cast<const T*>(nullptr), after);
            return;
        }
        if (!before && !after)
            return;
        changes.components.push_back({owner, after ? after->id : before->id, type,
                                      !before  ? SceneChangeKind::Added
                                      : !after ? SceneChangeKind::Removed
                                               : SceneChangeKind::Modified});
        changes.structureChanged |= !before || !after;
        auto oldAssets = AssetsOf<Assets>(before), newAssets = AssetsOf<Assets>(after);
        if (oldAssets != newAssets)
            changes.resources.push_back(
                {owner, after ? after->id : before->id, std::move(oldAssets), std::move(newAssets)});
    }

    void CompareComponents(SceneRuntime::SceneChangeSet& changes, const SceneRuntime::ScenePlacement* before,
                           const SceneRuntime::ScenePlacement* after)
    {
        using namespace SceneRuntime;
        const auto& owner = after ? after->id : before->id;
#define ENGINE_CAPTURE_OPTIONAL(Type, member, references, assets, order)                                               \
    CompareComponent<assets>(changes, owner, #member, before && before->member ? &*before->member : nullptr,           \
                             after && after->member ? &*after->member : nullptr);
#define ENGINE_CAPTURE_REPEATED(Type, member, references, assets, order)                                               \
    {                                                                                                                  \
        const std::vector<Type> empty;                                                                                 \
        const auto& previous = before ? before->member : empty;                                                        \
        const auto& current = after ? after->member : empty;                                                           \
        std::unordered_map<std::string_view, const Type*> indexed;                                                     \
        for (const auto& component : previous)                                                                         \
            indexed.emplace(component.id, &component);                                                                 \
        for (const auto& component : current)                                                                          \
        {                                                                                                              \
            const auto found = indexed.find(component.id);                                                             \
            CompareComponent<assets>(changes, owner, #member, found == indexed.end() ? nullptr : found->second,        \
                                     &component);                                                                      \
            if (found != indexed.end())                                                                                \
                indexed.erase(found);                                                                                  \
        }                                                                                                              \
        for (const auto& component : previous)                                                                         \
            if (indexed.contains(component.id))                                                                        \
                CompareComponent<assets>(changes, owner, #member, &component, static_cast<const Type*>(nullptr));      \
        if (previous.size() == current.size() && !std::equal(previous.begin(), previous.end(), current.begin(),        \
                                                             [](const auto& left, const auto& right)                   \
                                                             {                                                         \
                                                                 return left.id == right.id;                           \
                                                             }))                                                       \
        {                                                                                                              \
            changes.fields.push_back({owner, {}, #member ".order"});                                                   \
            changes.structureChanged = true;                                                                           \
        }                                                                                                              \
    }
        ENGINE_SCENE_COMPONENTS(ENGINE_CAPTURE_OPTIONAL, ENGINE_CAPTURE_REPEATED)
#undef ENGINE_CAPTURE_OPTIONAL
#undef ENGINE_CAPTURE_REPEATED
    }

    void CompareObjectFields(SceneRuntime::SceneChangeSet& changes, const SceneRuntime::ScenePlacement& before,
                             const SceneRuntime::ScenePlacement& after)
    {
        if (before.name != after.name)
            changes.fields.push_back({after.id, {}, "name"});
        if (before.persistent != after.persistent)
            changes.fields.push_back({after.id, {}, "persistent"});
        if (before.prefab != after.prefab)
        {
            changes.fields.push_back({after.id, {}, "prefab"});
            const auto previous = before.prefab ? before.prefab->asset : std::filesystem::path{};
            const auto current = after.prefab ? after.prefab->asset : std::filesystem::path{};
            if (previous != current)
                changes.resources.push_back({after.id,
                                             {},
                                             previous.empty() ? std::vector<std::filesystem::path>{}
                                                              : std::vector<std::filesystem::path>{previous},
                                             current.empty() ? std::vector<std::filesystem::path>{}
                                                             : std::vector<std::filesystem::path>{current}});
        }
        if (!before.SameComponents(after))
            CompareComponents(changes, &before, &after);
    }

    SceneRuntime::TransformField ChangedFields(const SceneRuntime::ScenePlacement& before,
                                               const SceneRuntime::ScenePlacement& after)
    {
        using SceneRuntime::TransformField;
        unsigned int fields = 0;
        if (before.position != after.position)
            fields |= static_cast<unsigned int>(TransformField::Position);
        if (before.rotation != after.rotation)
            fields |= static_cast<unsigned int>(TransformField::Rotation);
        if (before.scale != after.scale)
            fields |= static_cast<unsigned int>(TransformField::Scale);
        if (before.parentId != after.parentId)
            fields |= static_cast<unsigned int>(TransformField::Parent);
        return static_cast<TransformField>(fields);
    }
}

namespace SceneRuntime
{
    bool TransformChange::Has(TransformField field) const
    {
        return (static_cast<unsigned int>(fields) & static_cast<unsigned int>(field)) != 0;
    }

    bool SceneChangeSet::Empty() const noexcept
    {
        return transforms.empty() && objects.empty() && components.empty() && fields.empty() && resources.empty() &&
               !structureChanged;
    }

    void SceneChangeSet::Record(const ScenePlacement& before, const ScenePlacement& after)
    {
        if (before.id != after.id)
            throw std::invalid_argument("Transform changes require the same object ID");
        const auto changedFields = ChangedFields(before, after);
        if (changedFields == TransformField::None)
            return;
        structureChanged |= before.parentId != after.parentId;
        const auto found = std::find_if(transforms.begin(), transforms.end(),
                                        [&](const auto& change)
                                        {
                                            return change.objectId == after.id;
                                        });
        if (found == transforms.end())
            transforms.push_back({after.id, changedFields});
        else
            found->fields = static_cast<TransformField>(static_cast<unsigned int>(found->fields) |
                                                        static_cast<unsigned int>(changedFields));
    }

    void SceneChangeSet::Merge(const SceneChangeSet& changes)
    {
        if (&changes == this || changes.Empty())
            return;
        structureChanged |= changes.structureChanged;
        MergeUnique(objects, changes.objects);
        MergeUnique(components, changes.components);
        MergeUnique(fields, changes.fields);
        MergeUnique(resources, changes.resources);
        std::unordered_map<std::string, size_t> indices;
        indices.reserve(transforms.size() + changes.transforms.size());
        for (size_t index = 0; index < transforms.size(); ++index)
            indices.emplace(transforms[index].objectId, index);
        for (const auto& change : changes.transforms)
        {
            const auto [found, inserted] = indices.emplace(change.objectId, transforms.size());
            if (inserted)
                transforms.push_back(change);
            else
            {
                auto& changedFields = transforms[found->second].fields;
                changedFields = static_cast<TransformField>(static_cast<unsigned int>(changedFields) |
                                                            static_cast<unsigned int>(change.fields));
            }
        }
    }

    void SceneChangeSet::Swap(SceneChangeSet& changes) noexcept
    {
        transforms.swap(changes.transforms);
        objects.swap(changes.objects);
        components.swap(changes.components);
        fields.swap(changes.fields);
        resources.swap(changes.resources);
        std::swap(structureChanged, changes.structureChanged);
    }

    SceneChangeSet SceneChangeSet::CompareTransforms(const SceneLayout& before, const SceneLayout& after)
    {
        SceneChangeSet changes;
        const bool sameOrder = before.objects.size() == after.objects.size() &&
                               std::equal(before.objects.begin(), before.objects.end(), after.objects.begin(),
                                          [](const auto& left, const auto& right)
                                          {
                                              return left.id == right.id;
                                          });
        if (sameOrder)
        {
            for (size_t index = 0; index < before.objects.size(); ++index)
            {
                const auto fields = ChangedFields(before.objects[index], after.objects[index]);
                if (fields != TransformField::None)
                {
                    changes.transforms.push_back({after.objects[index].id, fields});
                    changes.structureChanged |= before.objects[index].parentId != after.objects[index].parentId;
                }
            }
            return changes;
        }
        std::unordered_map<std::string_view, const ScenePlacement*> previous;
        previous.reserve(before.objects.size());
        for (const auto& object : before.objects)
            previous.emplace(object.id, &object);
        for (const auto& object : after.objects)
            if (const auto found = previous.find(object.id); found != previous.end())
            {
                const auto fields = ChangedFields(*found->second, object);
                if (fields != TransformField::None)
                {
                    changes.transforms.push_back({object.id, fields});
                    changes.structureChanged |= found->second->parentId != object.parentId;
                }
            }
        return changes;
    }

    void SceneChangeSet::RecordObject(const ScenePlacement& before, const ScenePlacement& after)
    {
        Record(before, after);
        CompareObjectFields(*this, before, after);
    }

    void SceneChangeSet::RecordObject(const ScenePlacement& object, SceneChangeKind kind)
    {
        if (kind != SceneChangeKind::Added && kind != SceneChangeKind::Removed)
            throw std::invalid_argument("Object lifetime changes require Added or Removed");
        objects.push_back({object.id, kind});
        structureChanged = true;
        CompareComponents(*this, kind == SceneChangeKind::Removed ? &object : nullptr,
                          kind == SceneChangeKind::Added ? &object : nullptr);
        if (object.prefab && !object.prefab->asset.empty())
            resources.push_back(
                {object.id,
                 {},
                 kind == SceneChangeKind::Removed ? std::vector<std::filesystem::path>{object.prefab->asset}
                                                  : std::vector<std::filesystem::path>{},
                 kind == SceneChangeKind::Added ? std::vector<std::filesystem::path>{object.prefab->asset}
                                                : std::vector<std::filesystem::path>{}});
    }

    SceneChangeSet SceneChangeSet::Compare(const SceneLayout& before, const SceneLayout& after)
    {
        SceneChangeSet changes = CompareTransforms(before, after);
        std::unordered_map<std::string_view, const ScenePlacement*> indexed;
        indexed.reserve(before.objects.size());
        for (const auto& object : before.objects)
            indexed.emplace(object.id, &object);
        for (const auto& object : after.objects)
        {
            const auto found = indexed.find(object.id);
            if (found == indexed.end())
                changes.RecordObject(object, SceneChangeKind::Added);
            else
            {
                CompareObjectFields(changes, *found->second, object);
                indexed.erase(found);
            }
        }
        for (const auto& object : before.objects)
            if (indexed.contains(object.id))
                changes.RecordObject(object, SceneChangeKind::Removed);
        if (before.objects.size() == after.objects.size() &&
            !std::equal(before.objects.begin(), before.objects.end(), after.objects.begin(),
                        [](const auto& left, const auto& right)
                        {
                            return left.id == right.id;
                        }))
        {
            changes.fields.push_back({{}, {}, "objects.order"});
            changes.structureChanged = true;
        }
        if (before.settings != after.settings)
            changes.fields.push_back({{}, {}, "settings"});
        const auto shaders = [](const SceneSettings& settings)
        {
            std::vector<std::filesystem::path> paths;
            for (const auto& path :
                 {settings.skyShader, settings.oceanShader, settings.particleShader, settings.postShader})
                if (!path.empty())
                    paths.push_back(path);
            return paths;
        };
        if (const auto oldShaders = shaders(before.settings), newShaders = shaders(after.settings);
            oldShaders != newShaders)
            changes.resources.push_back({{}, {}, oldShaders, newShaders});
        if (before.assetReferences != after.assetReferences)
        {
            changes.fields.push_back({{}, {}, "assetReferences"});
            std::vector<std::filesystem::path> oldPaths, newPaths;
            for (const auto& [path, id] : before.assetReferences)
            {
                static_cast<void>(id);
                oldPaths.emplace_back(path);
            }
            for (const auto& [path, id] : after.assetReferences)
            {
                static_cast<void>(id);
                newPaths.emplace_back(path);
            }
            changes.resources.push_back({{}, {}, std::move(oldPaths), std::move(newPaths)});
        }
        if (before.prefabSlots != after.prefabSlots)
        {
            changes.fields.push_back({{}, {}, "prefabSlots"});
            changes.structureChanged = true;
        }
        if (before.sceneObjects != after.sceneObjects)
        {
            changes.fields.push_back({{}, {}, "sceneObjects"});
            changes.structureChanged = true;
        }
        return changes;
    }

    SceneEditResult SceneTransformEdit::Apply(ScenePlacement& object, const LocalTransform& transform)
    {
        for (const auto& values : {transform.position, transform.rotation, transform.scale})
            if (!std::all_of(values.begin(), values.end(),
                             [](float value)
                             {
                                 return std::isfinite(value);
                             }))
                return {};
        ScenePlacement candidate;
        candidate.id = object.id;
        candidate.parentId = object.parentId;
        candidate.position = transform.position;
        candidate.rotation = transform.rotation;
        candidate.scale = transform.scale;
        DirectX::XMFLOAT4X4 matrix;
        if (!SceneTransforms::Compose(candidate, matrix))
            return {};
        SceneEditResult result;
        result.changes.Record(object, candidate);
        object.position = transform.position;
        object.rotation = transform.rotation;
        object.scale = transform.scale;
        result.succeeded = true;
        return result;
    }

    WorldReadView::WorldReadView(const SceneLayout& layout) noexcept : layout_(&layout) {}

    WorldReadView::WorldReadView(std::shared_ptr<const Source> source) : source_(std::move(source))
    {
        if (!source_)
            throw std::invalid_argument("Missing world read source");
    }
    const ScenePlacement* WorldReadView::Find(std::string_view id) const
    {
        if (source_)
            return source_->Find(id);
        const auto found = std::find_if(layout_->objects.begin(), layout_->objects.end(),
                                        [&](const auto& object)
                                        {
                                            return object.id == id;
                                        });
        return found == layout_->objects.end() ? nullptr : &*found;
    }

    std::optional<ScenePlacement> WorldReadView::Object(std::string_view id) const
    {
        if (source_)
            return source_->Object(id);
        const auto* object = Find(id);
        return object ? std::optional<ScenePlacement>{*object} : std::nullopt;
    }

    std::vector<std::string> WorldReadView::ObjectIds() const
    {
        if (source_)
            return source_->ObjectIds();
        std::vector<std::string> ids;
        ids.reserve(layout_->objects.size());
        for (const auto& object : layout_->objects)
            ids.push_back(object.id);
        return ids;
    }

    SceneSettings WorldReadView::Settings() const
    {
        if (source_)
            return source_->Settings();
        return layout_->settings;
    }

    std::optional<LocalTransform> WorldReadView::Transform(std::string_view id) const
    {
        if (source_)
            return source_->Transform(id);
        const auto found = std::find_if(layout_->objects.begin(), layout_->objects.end(),
                                        [&](const auto& object)
                                        {
                                            return object.id == id;
                                        });
        if (found == layout_->objects.end())
            return std::nullopt;
        return LocalTransform{found->position, found->rotation, found->scale};
    }
}
