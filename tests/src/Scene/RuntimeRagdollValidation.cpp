#include <SceneRuntime/Physics/RuntimeRagdoll.h>
#include <SceneRuntime/Scene/SceneTransforms.h>
#include <Engine/Core/Profiler.h>
#include <iostream>

#if defined(__d3d12_h__) || defined(_WINDOWS_)
#error Runtime ragdoll must not depend on platform or rendering APIs
#endif

namespace
{
    using namespace SceneRuntime;
    size_t contracts = 0;
    void Require(bool value, const char* name)
    {
        ++contracts;
        if (!value)
            throw std::runtime_error(name);
        std::cout << "PASS: " << name << std::endl;
    }
    void Validate()
    {
        SceneLayout source;
        ScenePlacement owner, body;
        owner.id = "owner";
        owner.position = {3, 0, 0};
        owner.meshRenderer = MeshRendererComponent{};
        owner.meshRenderer->model = "Assets/Models/missing.gltf";
        owner.animator = AnimatorComponent{};
        owner.ragdoll = RagdollComponent{};
        owner.ragdoll->bones.push_back({"root", "bone-body"});
        body.id = "bone-body";
        body.parentId = owner.id;
        body.position = {1, 0, 0};
        body.boxCollider = BoxColliderComponent{};
        body.rigidBody = RigidBodyComponent{};
        source.objects = {owner, body};
        const auto definition = WorldDefinition::Compile(source);
        WorldState world(definition);
        Engine::SkeletonData rig;
        DirectX::XMStoreFloat4x4(&rig.inverseRoot, DirectX::XMMatrixIdentity());
        Engine::SkeletonNode bone;
        bone.name = "root";
        bone.rest.position = {2, 0, 0};
        rig.nodes.push_back(bone);
        const auto entity = *world.Read().Resolve(owner.id), child = *world.Read().Resolve(body.id);
        RuntimeRagdoll::Validate(world.Read(), entity, rig);
        Engine::CpuProfiler::Take();
        std::string error;
        {
            auto transaction = world.BeginTransaction();
            RuntimeRagdoll::Synchronize(transaction, entity, rig, {});
            Require(transaction.Read().Transform(child)->position[0] == 2 &&
                        transaction.Read().Component<RigidBodyComponent>(child)->motion == "kinematic" &&
                        world.Read().Transform(child)->position[0] == 1,
                    "runtime-ragdoll-stages-bone-following");
            Require(transaction.Prepare(error) && transaction.Commit(), "runtime-ragdoll-commits-bone-following");
        }
        {
            auto transaction = world.BeginTransaction();
            auto settings = transaction.Read().Component<RagdollComponent>(entity);
            settings->active = true;
            Require(transaction.SetComponent(entity, settings, error).succeeded, "runtime-ragdoll-stages-activation");
            RuntimeRagdoll::Synchronize(transaction, entity, rig, {});
            Require(transaction.Read().Component<RigidBodyComponent>(child)->motion == "dynamic",
                    "runtime-ragdoll-activation-enables-physics");
            auto moved = *transaction.Read().Transform(child);
            moved.position[0] = 4;
            transaction.SetLocalTransform(child, moved, error);
            const auto pose = RuntimeRagdoll::Pose(transaction.Read(), entity, rig, {});
            Require(pose.size() == 1 && pose[0].position[0] == 4, "runtime-ragdoll-pose-reads-candidate-physics");
        }
        Require(world.Read().Transform(child)->position[0] == 2 &&
                    !world.Read().Component<RagdollComponent>(entity)->active,
                "runtime-ragdoll-abort-retains-pose-and-mode");
        bool exported = false;
        for (const auto& sample : Engine::CpuProfiler::Take())
            exported |= sample.name == "Runtime authoring export";
        Require(!exported && definition->Source().objects[1].position[0] == 1,
                "runtime-ragdoll-keeps-authoring-data-immutable");
    }
}
int main()
{
    try
    {
        Validate();
        std::cout << "Runtime ragdoll contracts: " << contracts << ", failures: 0\n";
        return 0;
    }
    catch (const std::exception& failure)
    {
        std::cerr << "FAIL: " << failure.what() << '\n';
        return 1;
    }
}
