#include <SceneRuntime/Physics/RuntimeRagdoll.h>
#include <SceneRuntime/Scene/SceneTransforms.h>
#include <algorithm>
#include <cstring>
#include <set>

namespace
{
    using namespace SceneRuntime;
    using Matrix = DirectX::XMFLOAT4X4;
    size_t Bone(const Engine::SkeletonData& rig, const std::string& name)
    {
        const auto found = std::find_if(rig.nodes.begin(), rig.nodes.end(),
                                        [&](const auto& item)
                                        {
                                            return item.name == name;
                                        });
        if (found == rig.nodes.end() || std::count_if(rig.nodes.begin(), rig.nodes.end(),
                                                      [&](const auto& item)
                                                      {
                                                          return item.name == name;
                                                      }) != 1)
            throw std::runtime_error("Missing or ambiguous ragdoll bone: " + name);
        return static_cast<size_t>(found - rig.nodes.begin());
    }
    EntityHandle Body(WorldState::ReadView view, const std::string& id)
    {
        const auto entity = view.Resolve(id);
        if (!entity)
            throw std::runtime_error("Ragdoll body missing: " + id);
        return *entity;
    }
    Matrix Offset(const RagdollBone& binding)
    {
        Matrix result;
        std::memcpy(&result, binding.bindOffset.data(), sizeof(result));
        return result;
    }
    DirectX::XMMATRIX Import(const Engine::SkeletonData& rig)
    {
        return DirectX::XMLoadFloat4x4(&rig.inverseRoot) *
               DirectX::XMMatrixScaling(rig.importScale, rig.importScale, rig.importScale);
    }
    std::vector<Engine::BonePose> BasePose(const Engine::SkeletonData& rig,
                                           const std::vector<Engine::BonePose>& current)
    {
        if (current.size() == rig.nodes.size())
            return current;
        std::vector<Engine::BonePose> result;
        for (const auto& bone : rig.nodes)
            result.push_back(bone.rest);
        return result;
    }
}
namespace SceneRuntime
{
    void RuntimeRagdoll::Validate(WorldState::ReadView view, EntityHandle owner, const Engine::SkeletonData& rig)
    {
        const auto component = view.ComponentLease<RagdollComponent>(owner);
        if (!component)
            return;
        component->Validate();
        if (!view.ComponentLease<AnimatorComponent>(owner) || !view.ComponentLease<MeshRendererComponent>(owner) ||
            rig.nodes.empty())
            throw std::runtime_error("Ragdoll requires an Animator and skeleton model");
        for (const auto& binding : component->bones)
        {
            Bone(rig, binding.bone);
            const auto body = Body(view, binding.body);
            if (body == owner || !view.ComponentLease<RigidBodyComponent>(body) ||
                !view.ComponentLease<BoxColliderComponent>(body))
                throw std::runtime_error("Ragdoll binding requires a separate Collider and RigidBody");
        }
    }
    void RuntimeRagdoll::Synchronize(WorldState::Transaction& transaction, EntityHandle owner,
                                     const Engine::SkeletonData& rig, const std::vector<Engine::BonePose>& current)
    {
        const auto view = transaction.Read();
        const auto component = view.ComponentLease<RagdollComponent>(owner);
        if (!component)
            return;
        Validate(view, owner, rig);
        const auto ownerWorld = *view.WorldMatrix(owner);
        const auto bones = Engine::Skeleton::Matrices(rig, BasePose(rig, current));
        const bool active = component->enabled && component->active;
        if (active)
            if (auto body = view.Component<RigidBodyComponent>(owner))
            {
                body->velocity = body->angularVelocity = {0, 0, 0};
                std::string error;
                if (!transaction.SetComponent(owner, std::move(body), error).succeeded)
                    throw std::runtime_error(error);
            }
        struct Binding
        {
            EntityHandle entity;
            Matrix parent;
            size_t bone;
            Matrix offset;
        };
        std::vector<Binding> bindings;
        for (const auto& binding : component->bones)
        {
            const auto body = Body(view, binding.body);
            Matrix parent;
            DirectX::XMStoreFloat4x4(&parent, DirectX::XMMatrixIdentity());
            if (const auto handle = view.Parent(body))
                parent = *view.WorldMatrix(*handle);
            bindings.push_back({body, parent, Bone(rig, binding.bone), Offset(binding)});
        }
        std::string error;
        for (const auto& binding : bindings)
        {
            auto body = view.Component<RigidBodyComponent>(binding.entity);
            body->motion = active ? "dynamic" : "kinematic";
            if (!active)
                body->velocity = body->angularVelocity = {0, 0, 0};
            if (!transaction.SetComponent(binding.entity, std::move(body), error).succeeded)
                throw std::runtime_error(error);
            if (auto joint = view.Component<JointComponent>(binding.entity))
            {
                joint->enabled = active;
                if (!transaction.SetComponent(binding.entity, std::move(joint), error).succeeded)
                    throw std::runtime_error(error);
            }
            if (active)
                continue;
            Matrix world, local;
            DirectX::XMStoreFloat4x4(&world,
                                     DirectX::XMMatrixInverse(nullptr, DirectX::XMLoadFloat4x4(&binding.offset)) *
                                         DirectX::XMLoadFloat4x4(&bones[binding.bone]) * Import(rig) *
                                         DirectX::XMLoadFloat4x4(&ownerWorld));
            LocalTransform transformed;
            if (!SceneTransforms::WorldToLocal(world, binding.parent, local) ||
                !SceneTransforms::ReadTransform(local, *view.Transform(binding.entity), transformed) ||
                !transaction.SetLocalTransform(binding.entity, transformed, error).succeeded)
                throw std::runtime_error("Ragdoll body cannot follow bone transform");
        }
    }
    std::vector<Engine::BonePose> RuntimeRagdoll::Pose(WorldState::ReadView view, EntityHandle owner,
                                                       const Engine::SkeletonData& rig,
                                                       const std::vector<Engine::BonePose>& current)
    {
        auto pose = BasePose(rig, current);
        const auto component = view.ComponentLease<RagdollComponent>(owner);
        if (!component || !component->enabled || !component->active || component->bones.empty())
            return pose;
        Validate(view, owner, rig);
        const auto ownerWorld = *view.WorldMatrix(owner);
        auto bones = Engine::Skeleton::Matrices(rig, pose);
        const auto toBone = DirectX::XMMatrixInverse(nullptr, DirectX::XMLoadFloat4x4(&ownerWorld)) *
                            DirectX::XMMatrixInverse(nullptr, Import(rig));
        std::set<size_t> overridden;
        for (const auto& binding : component->bones)
        {
            const auto bone = Bone(rig, binding.bone);
            const auto offset = Offset(binding);
            const auto matrix = *view.WorldMatrix(Body(view, binding.body));
            DirectX::XMStoreFloat4x4(&bones[bone],
                                     DirectX::XMLoadFloat4x4(&offset) * DirectX::XMLoadFloat4x4(&matrix) * toBone);
            overridden.insert(bone);
        }
        auto target = pose;
        for (size_t bone = 0; bone < rig.nodes.size(); ++bone)
        {
            const auto parent = rig.nodes[bone].parent;
            if (!overridden.contains(bone))
            {
                auto local = DirectX::XMMatrixScaling(pose[bone].scale[0], pose[bone].scale[1], pose[bone].scale[2]) *
                             DirectX::XMMatrixRotationQuaternion(
                                 DirectX::XMVectorSet(pose[bone].rotation[0], pose[bone].rotation[1],
                                                      pose[bone].rotation[2], pose[bone].rotation[3])) *
                             DirectX::XMMatrixTranslation(pose[bone].position[0], pose[bone].position[1],
                                                          pose[bone].position[2]);
                if (parent >= 0)
                    local *= DirectX::XMLoadFloat4x4(&bones[static_cast<size_t>(parent)]);
                DirectX::XMStoreFloat4x4(&bones[bone], local);
                continue;
            }
            auto local = DirectX::XMLoadFloat4x4(&bones[bone]);
            if (parent >= 0)
                local *=
                    DirectX::XMMatrixInverse(nullptr, DirectX::XMLoadFloat4x4(&bones[static_cast<size_t>(parent)]));
            DirectX::XMVECTOR scale, rotation, position;
            if (!DirectX::XMMatrixDecompose(&scale, &rotation, &position, local))
                throw std::runtime_error("Ragdoll pose cannot be decomposed");
            DirectX::XMFLOAT3 s, p;
            DirectX::XMFLOAT4 q;
            DirectX::XMStoreFloat3(&s, scale);
            DirectX::XMStoreFloat3(&p, position);
            DirectX::XMStoreFloat4(&q, DirectX::XMQuaternionNormalize(rotation));
            target[bone].scale = {s.x, s.y, s.z};
            target[bone].position = {p.x, p.y, p.z};
            target[bone].rotation = {q.x, q.y, q.z, q.w};
        }
        return Engine::Skeleton::Blend(pose, target, component->weight);
    }
}
