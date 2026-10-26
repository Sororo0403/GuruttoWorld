#include <SceneRuntime/Scene/SceneEdits.h>
#include <SceneRuntime/Scene/SceneTransforms.h>
#include <SceneRuntime/Scene/ObjectReferenceDomain.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace
{
    using namespace SceneRuntime;

    bool Rotation(const DirectX::XMFLOAT4X4& value)
    {
        if (!SceneTransforms::IsUsable(value))
            return false;
        const auto matrix = DirectX::XMLoadFloat4x4(&value);
        DirectX::XMFLOAT4X4 product, identity;
        DirectX::XMStoreFloat4x4(&product, matrix * DirectX::XMMatrixTranspose(matrix));
        DirectX::XMStoreFloat4x4(&identity, DirectX::XMMatrixIdentity());
        return DirectX::XMVectorGetX(DirectX::XMMatrixDeterminant(matrix)) > 0 &&
               SceneTransforms::Matches(product, identity);
    }

    class Editor final
    {
    public:
        const SceneLayout& before;
        SceneEditPlan plan;

        ScenePlacement& Find(const std::string& id)
        {
            const auto found = std::find_if(plan.layout.objects.begin(), plan.layout.objects.end(),
                                            [&](const auto& object)
                                            {
                                                return object.id == id;
                                            });
            if (found == plan.layout.objects.end())
                throw std::runtime_error("Object no longer exists: " + id);
            return *found;
        }

        std::vector<size_t> Selection(const std::vector<std::string>& ids)
        {
            if (ids.empty())
                throw std::runtime_error("No objects selected");
            std::vector<size_t> indices;
            for (const auto& id : ids)
            {
                const auto index = static_cast<size_t>(&Find(id) - plan.layout.objects.data());
                if (std::find(indices.begin(), indices.end(), index) == indices.end())
                    indices.push_back(index);
            }
            return indices;
        }

        std::string NewId()
        {
            const auto reserved = ObjectReferenceDomain(plan.layout);
            for (;;)
            {
                const auto id = "object-" + std::to_string(plan.nextId++);
                if (!reserved.contains(id))
                    return id;
            }
        }

        std::vector<DirectX::XMFLOAT4X4> Matrices() const
        {
            std::vector<DirectX::XMFLOAT4X4> matrices;
            std::string error;
            if (!SceneTransforms::Resolve(before, matrices, error))
                throw std::runtime_error(error);
            return matrices;
        }

        DirectX::XMFLOAT4X4 Matrix(const std::string& id, const std::vector<DirectX::XMFLOAT4X4>& matrices) const
        {
            for (size_t index = 0; index < before.objects.size(); ++index)
                if (before.objects[index].id == id)
                    return matrices[index];
            throw std::runtime_error("Object no longer exists: " + id);
        }

        void Local(ScenePlacement& object, DirectX::XMFLOAT4X4 world, const std::string& parent,
                   const std::vector<DirectX::XMFLOAT4X4>& matrices) const
        {
            if (!parent.empty() && !SceneTransforms::WorldToLocal(world, Matrix(parent, matrices), world))
                throw std::runtime_error(object.id + ": invalid parent transform");
            auto candidate = object;
            if (!SceneTransforms::ReadTransform(world, object, candidate))
                throw std::runtime_error(object.id + ": world placement requires shear that cannot be stored as SRT");
            candidate.parentId = parent;
            object = std::move(candidate);
        }

        bool SelectedAncestor(std::string parent, const std::vector<std::string>& ids)
        {
            while (!parent.empty())
            {
                if (std::find(ids.begin(), ids.end(), parent) != ids.end())
                    return true;
                const auto found = std::find_if(before.objects.begin(), before.objects.end(),
                                                [&](const auto& object)
                                                {
                                                    return object.id == parent;
                                                });
                if (found == before.objects.end())
                    return false;
                parent = found->parentId;
            }
            return false;
        }

        void Translate(ScenePlacement& object, const std::array<float, 3>& delta,
                       const std::vector<DirectX::XMFLOAT4X4>& matrices) const
        {
            auto movement = DirectX::XMVectorSet(delta[0], delta[1], delta[2], 0);
            if (!object.parentId.empty())
            {
                const auto parent = Matrix(object.parentId, matrices);
                movement = DirectX::XMVector3TransformNormal(
                    movement, DirectX::XMMatrixInverse(nullptr, DirectX::XMLoadFloat4x4(&parent)));
            }
            DirectX::XMFLOAT3 local;
            DirectX::XMStoreFloat3(&local, movement);
            object.position[0] += local.x;
            object.position[1] += local.y;
            object.position[2] += local.z;
        }

        void Apply(const TransformEdit& edit)
        {
            if (!SceneTransformEdit::Apply(Find(edit.id), edit.value).succeeded)
                throw std::runtime_error("Invalid local transform");
        }
        void Apply(const WorldTransformEdit& edit)
        {
            if (!SceneTransforms::IsUsable(edit.delta))
                throw std::runtime_error("Invalid world transform");
            const auto matrices = Matrices();
            const auto indices = Selection(edit.ids);
            if (indices.size() != edit.ids.size())
                throw std::runtime_error("Duplicate selection");
            DirectX::XMFLOAT4X4 identity;
            DirectX::XMStoreFloat4x4(&identity, DirectX::XMMatrixIdentity());
            bool unchanged = true;
            for (size_t row = 0; row < 4; ++row)
                for (size_t column = 0; column < 4; ++column)
                    unchanged &= edit.delta.m[row][column] == identity.m[row][column];
            for (const auto index : indices)
            {
                auto& object = plan.layout.objects[index];
                if (unchanged || SelectedAncestor(object.parentId, edit.ids))
                    continue;
                auto world = matrices[index];
                DirectX::XMStoreFloat4x4(&world,
                                         DirectX::XMLoadFloat4x4(&world) * DirectX::XMLoadFloat4x4(&edit.delta));
                Local(object, world, object.parentId, matrices);
            }
        }
        void Apply(const TranslationEdit& edit)
        {
            if (!std::all_of(edit.delta.begin(), edit.delta.end(),
                             [](float value)
                             {
                                 return std::isfinite(value);
                             }))
                throw std::runtime_error("Invalid world translation");
            const auto indices = Selection(edit.ids);
            if (indices.size() != edit.ids.size())
                throw std::runtime_error("Duplicate selection");
            const auto matrices = Matrices();
            for (const auto index : indices)
            {
                auto& object = plan.layout.objects[index];
                if (!SelectedAncestor(object.parentId, edit.ids))
                    Translate(object, edit.delta, matrices);
            }
        }
        void Apply(const ParentEdit& edit)
        {
            const auto matrices = Matrices();
            for (const auto index : Selection(edit.ids))
            {
                auto& object = plan.layout.objects[index];
                if (object.parentId != edit.parent)
                    Local(object, matrices[index], edit.parent, matrices);
            }
        }
        void Apply(const RotationEdit& edit)
        {
            if (!Rotation(edit.rotation) || !std::all_of(edit.pivot.begin(), edit.pivot.end(),
                                                         [](float value)
                                                         {
                                                             return std::isfinite(value);
                                                         }))
                throw std::runtime_error("Invalid rotation or pivot");
            DirectX::XMFLOAT4X4 delta;
            DirectX::XMStoreFloat4x4(&delta,
                                     DirectX::XMMatrixTranslation(-edit.pivot[0], -edit.pivot[1], -edit.pivot[2]) *
                                         DirectX::XMLoadFloat4x4(&edit.rotation) *
                                         DirectX::XMMatrixTranslation(edit.pivot[0], edit.pivot[1], edit.pivot[2]));
            Apply(WorldTransformEdit{edit.ids, delta});
        }
        void Apply(const ScaleEdit& edit)
        {
            if (!Rotation(edit.axes) ||
                !std::all_of(edit.pivot.begin(), edit.pivot.end(),
                             [](float value)
                             {
                                 return std::isfinite(value);
                             }) ||
                !std::all_of(edit.factors.begin(), edit.factors.end(),
                             [](float value)
                             {
                                 return std::isfinite(value) && value > 0;
                             }))
                throw std::runtime_error("Invalid scale, axes or pivot");
            auto scaling = DirectX::XMMatrixScaling(edit.factors[0], edit.factors[1], edit.factors[2]);
            if (edit.factors[0] != edit.factors[1] || edit.factors[1] != edit.factors[2])
            {
                const auto axes = DirectX::XMLoadFloat4x4(&edit.axes);
                scaling = DirectX::XMMatrixTranspose(axes) * scaling * axes;
            }
            DirectX::XMFLOAT4X4 delta;
            DirectX::XMStoreFloat4x4(
                &delta, DirectX::XMMatrixTranslation(-edit.pivot[0], -edit.pivot[1], -edit.pivot[2]) * scaling *
                            DirectX::XMMatrixTranslation(edit.pivot[0], edit.pivot[1], edit.pivot[2]));
            Apply(WorldTransformEdit{edit.ids, delta});
        }
        void Apply(const NameEdit& edit)
        {
            if (edit.name.find_first_not_of(" \t\r\n") == std::string::npos ||
                edit.name.find('\0') != std::string::npos)
                throw std::runtime_error("Invalid object name");
            Find(edit.id).name = edit.name;
        }
        void Apply(const ComponentsEdit& edit)
        {
            for (const auto& value : edit.values)
            {
                auto& object = Find(value.id);
                object.CopyComponents(value);
                if (!object.camera && plan.layout.settings.mainCamera == object.id)
                    plan.layout.settings.mainCamera.clear();
            }
        }
        void Apply(const SettingsEdit& edit)
        {
            plan.layout.settings = edit.value;
        }
        void Apply(const AddObjectEdit& edit)
        {
            auto object = edit.value;
            if (object.id.empty())
                object.id = NewId();
            if (std::any_of(plan.layout.objects.begin(), plan.layout.objects.end(),
                            [&](const auto& other)
                            {
                                return other.id == object.id;
                            }))
                throw std::runtime_error("Object ID already exists");
            if (object.name.empty())
                object.name = object.meshRenderer ? object.Model().stem().string() : "Empty object";
            plan.createdIds.push_back(object.id);
            plan.layout.objects.push_back(std::move(object));
        }
        void Apply(const DuplicateEdit& edit)
        {
            if (!std::all_of(edit.offset.begin(), edit.offset.end(),
                             [](float value)
                             {
                                 return std::isfinite(value);
                             }))
                throw std::runtime_error("Invalid world duplicate offset");
            const auto indices = Selection(edit.ids);
            const auto matrices = Matrices();
            std::unordered_map<std::string, std::string> copies;
            for (const auto index : indices)
            {
                auto id = NewId();
                copies.emplace(before.objects[index].id, id);
                plan.createdIds.push_back(std::move(id));
            }
            for (const auto index : indices)
            {
                auto object = before.objects[index];
                const auto original = object.id;
                object.RemapComponentReferences(copies);
                if (object.prefab)
                {
                    const auto root = copies.find(object.prefab->rootId);
                    if (root != copies.end())
                        object.prefab->rootId = root->second;
                    else
                        object.prefab.reset();
                }
                const auto parent = copies.find(object.parentId);
                if (parent != copies.end())
                    object.parentId = parent->second;
                else
                {
                    Translate(object, edit.offset, matrices);
                }
                object.id = copies.at(original);
                object.name += " copy";
                for (auto& [name, members] : plan.layout.sceneObjects)
                {
                    static_cast<void>(name);
                    if (std::find(members.begin(), members.end(), original) != members.end())
                    {
                        members.push_back(object.id);
                        break;
                    }
                }
                plan.layout.objects.push_back(std::move(object));
            }
        }
        void Apply(const RemoveObjectsEdit& edit)
        {
            static_cast<void>(Selection(edit.ids));
            const std::unordered_set<std::string> removed(edit.ids.begin(), edit.ids.end());
            const auto matrices = Matrices();
            for (size_t index = 0; index < plan.layout.objects.size(); ++index)
            {
                auto& object = plan.layout.objects[index];
                if (removed.contains(object.id))
                    continue;
                if (object.prefab && removed.contains(object.prefab->rootId))
                    object.prefab.reset();
                if (removed.contains(object.parentId))
                    Local(object, matrices[index], {}, matrices);
            }
            std::erase_if(plan.layout.objects,
                          [&](const auto& object)
                          {
                              return removed.contains(object.id);
                          });
            for (auto& [name, members] : plan.layout.sceneObjects)
            {
                static_cast<void>(name);
                std::erase_if(members,
                              [&](const auto& id)
                              {
                                  return removed.contains(id);
                              });
            }
            if (removed.contains(plan.layout.settings.mainCamera))
                plan.layout.settings.mainCamera.clear();
        }
        void Apply(const ReplaceDocumentEdit& edit)
        {
            plan.layout = edit.value;
        }
    };
}

namespace SceneRuntime
{
    std::optional<SceneEditPlan> SceneEdits::Prepare(const SceneLayout& before, const SceneEdit& edit, size_t nextId,
                                                     std::string& error)
    {
        try
        {
            Editor editor{before, {before, {}, {}, nextId}};
            std::visit(
                [&](const auto& operation)
                {
                    editor.Apply(operation);
                },
                edit);
            editor.plan.layout.Validate();
            editor.plan.changes = SceneChangeSet::Compare(before, editor.plan.layout);
            error.clear();
            return std::move(editor.plan);
        }
        catch (const std::exception& failure)
        {
            error = failure.what();
        }
        catch (...)
        {
            error = "Document edit preparation failed";
        }
        return std::nullopt;
    }
}
