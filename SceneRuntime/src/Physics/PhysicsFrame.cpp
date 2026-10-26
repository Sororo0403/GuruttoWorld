#include "PhysicsFrame.h"

namespace SceneRuntime
{
    PhysicsFrame PhysicsFrame::Capture(WorldState::ReadView view, const std::map<std::string, bool>& rootMotions)
    {
        PhysicsFrame frame;
        frame.settings.gravity = view.Definition()->Source().settings.gravity;
        frame.objects.reserve(view.Entities().size());
        for (const auto entity : view.Entities())
        {
            PhysicsObject object;
            object.entity = entity;
            object.id = *view.ObjectId(entity);
            if (const auto parent = view.Parent(entity))
                object.parentId = *view.ObjectId(*parent);
            const auto transform = *view.Transform(entity);
            object.position = transform.position;
            object.rotation = transform.rotation;
            object.scale = transform.scale;
            object.boxCollider = view.ComponentLease<BoxColliderComponent>(entity);
            object.colliderInstance = view.Instance<BoxColliderComponent>(entity).value_or(ComponentInstanceHandle{});
            object.rigidBody = view.ComponentLease<RigidBodyComponent>(entity);
            object.bodyInstance = view.Instance<RigidBodyComponent>(entity).value_or(ComponentInstanceHandle{});
            object.playerController = view.ComponentLease<PlayerControllerComponent>(entity);
            object.controllerInstance =
                view.Instance<PlayerControllerComponent>(entity).value_or(ComponentInstanceHandle{});
            object.joint = view.ComponentLease<JointComponent>(entity);
            object.jointInstance = view.Instance<JointComponent>(entity).value_or(ComponentInstanceHandle{});
            object.terrain = view.ComponentLease<TerrainComponent>(entity);
            object.tilemap = view.ComponentLease<TilemapComponent>(entity);
            object.meshRenderer = view.ComponentLease<MeshRendererComponent>(entity);
            if (const auto animator = view.ComponentLease<AnimatorComponent>(entity))
                object.animator = PhysicsObject::Motion{animator->enabled, animator->rootMotion};
            if (object.animator)
                if (const auto found = rootMotions.find(object.id); found != rootMotions.end())
                    object.animator->rootMotion = found->second;
            const auto ragdoll = view.ComponentLease<RagdollComponent>(entity);
            if (ragdoll && ragdoll->enabled && ragdoll->active && !ragdoll->bones.empty())
            {
                if (object.boxCollider)
                {
                    auto disabled = *object.boxCollider;
                    disabled.enabled = false;
                    object.boxCollider = std::make_shared<const BoxColliderComponent>(disabled);
                }
                if (object.playerController)
                {
                    auto disabled = *object.playerController;
                    disabled.enabled = false;
                    object.playerController = std::make_shared<const PlayerControllerComponent>(disabled);
                }
                if (object.rigidBody)
                {
                    auto disabled = *object.rigidBody;
                    disabled.enabled = false;
                    disabled.velocity = disabled.angularVelocity = {0, 0, 0};
                    object.rigidBody = std::make_shared<const RigidBodyComponent>(disabled);
                }
            }
            frame.objects.push_back(std::move(object));
        }
        return frame;
    }
    PhysicsFrame PhysicsFrame::Capture(const SceneLayout& layout)
    {
        PhysicsFrame frame;
        frame.settings.gravity = layout.settings.gravity;
        frame.objects.reserve(layout.objects.size());
        for (const auto& value : layout.objects)
        {
            PhysicsObject object;
            object.id = value.id;
            object.parentId = value.parentId;
            object.position = value.position;
            object.rotation = value.rotation;
            object.scale = value.scale;
            object.boxCollider = value.boxCollider;
            object.rigidBody = value.rigidBody;
            object.playerController = value.playerController;
            object.joint = value.joint;
            object.terrain = value.terrain;
            object.tilemap = value.tilemap;
            object.meshRenderer = value.meshRenderer;
            if (value.animator)
                object.animator = PhysicsObject::Motion{value.animator->enabled, value.animator->rootMotion};
            frame.objects.push_back(std::move(object));
        }
        return frame;
    }
    bool PhysicsTransforms::Compose(const PhysicsObject& object, DirectX::XMFLOAT4X4& output)
    {
        return SceneTransforms::Compose(LocalTransform{object.position, object.rotation, object.scale}, output);
    }
    bool PhysicsTransforms::ReadTransform(const DirectX::XMFLOAT4X4& matrix, const PhysicsObject& reference,
                                          PhysicsObject& output)
    {
        LocalTransform value;
        if (!SceneTransforms::ReadTransform(
                matrix, LocalTransform{reference.position, reference.rotation, reference.scale}, value))
            return false;
        auto candidate = reference;
        candidate.position = value.position;
        candidate.rotation = value.rotation;
        candidate.scale = value.scale;
        output = std::move(candidate);
        return true;
    }
    bool PhysicsTransforms::Resolve(const PhysicsFrame& frame, std::vector<DirectX::XMFLOAT4X4>& output,
                                    std::string& error)
    {
        try
        {
            const auto count = frame.objects.size();
            std::unordered_map<std::string, size_t> indices;
            for (size_t index = 0; index < count; ++index)
                if (frame.objects[index].id.empty() || !indices.emplace(frame.objects[index].id, index).second)
                    throw std::runtime_error("Invalid physics entity ID");
            std::vector<DirectX::XMFLOAT4X4> matrices(count);
            std::vector<size_t> parents(count, count), chain;
            std::vector<unsigned char> visited(count);
            for (size_t index = 0; index < count; ++index)
            {
                const auto& object = frame.objects[index];
                if (!object.parentId.empty())
                    parents[index] = indices.at(object.parentId);
                if (!Compose(object, matrices[index]))
                    throw std::runtime_error("Invalid physics local transform");
            }
            for (size_t index = 0; index < count; ++index)
            {
                auto current = index;
                while (current != count && !visited[current])
                {
                    visited[current] = 1;
                    chain.push_back(current);
                    current = parents[current];
                }
                if (current != count && visited[current] == 1)
                    throw std::runtime_error("Physics parent cycle");
                while (!chain.empty())
                {
                    current = chain.back();
                    chain.pop_back();
                    if (parents[current] != count)
                        DirectX::XMStoreFloat4x4(&matrices[current],
                                                 DirectX::XMLoadFloat4x4(&matrices[current]) *
                                                     DirectX::XMLoadFloat4x4(&matrices[parents[current]]));
                    if (!SceneTransforms::IsUsable(matrices[current]))
                        throw std::runtime_error("Invalid inherited physics transform");
                    visited[current] = 2;
                }
            }
            output.swap(matrices);
            error.clear();
            return true;
        }
        catch (const std::exception& failure)
        {
            error = failure.what();
            return false;
        }
    }
}
