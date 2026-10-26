#include <SceneRuntime/Animation/RuntimeAnimation.h>
#include <SceneRuntime/Scene/RuntimePipeline.h>
#include <Engine/Core/Profiler.h>
#include <iostream>

#if defined(__d3d12_h__) || defined(_WINDOWS_)
#error Runtime animation must not include platform or rendering APIs
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
        auto rig = std::make_shared<Engine::SkeletonData>();
        DirectX::XMStoreFloat4x4(&rig->inverseRoot, DirectX::XMMatrixIdentity());
        Engine::SkeletonNode bone;
        bone.name = "root";
        rig->nodes.push_back(bone);
        Engine::SkeletalClip clip;
        clip.name = "Walk";
        clip.duration = 1;
        clip.tracks[0].positions = {{0, {0, 0, 0}}, {1, {1, 0, 0}}};
        rig->clips.push_back(clip);
        ScenePlacement actor;
        actor.id = "actor";
        actor.meshRenderer = MeshRendererComponent{};
        actor.meshRenderer->model = "Assets/Models/actor.gltf";
        actor.animator = AnimatorComponent{};
        actor.animator->states = {{"Idle", "Walk", 1, true}};
        actor.animator->rootMotion = true;
        actor.animator->rootBone = "root";
        AnimatorEventKey event;
        event.clip = "Walk";
        event.name = "Step";
        event.time = .05f;
        actor.animator->events.push_back(event);
        ScriptComponent listener;
        listener.behaviour = "Listener";
        actor.scripts.push_back(listener);
        SceneLayout source;
        source.objects.push_back(actor);
        auto definitions = std::make_shared<RuntimeScriptDefinitions>();
        size_t events = 0;
        (*definitions)["Listener"].onEvent = [&](RuntimeScriptContext&)
        {
            ++events;
        };
        (*definitions)["Listener"].lateUpdate = [](RuntimeScriptContext&) {};
        (*definitions)["Listener"].update = [](RuntimeScriptContext& context)
        {
            if (context.Pressed("disable"))
                context.scene.SetRootMotion(context.entity, false);
            if (context.Pressed("enable"))
                context.scene.SetRootMotion(context.entity, true);
        };
        const auto definition = WorldDefinition::Compile(source);
        RuntimeSession session(definition, definitions);
        RuntimeAnimation* observer = nullptr;
        bool fail = false;
        std::vector<RuntimeSkinPose> middle;
        RuntimeAnimationServices services;
        services.load = [&](const std::filesystem::path&, std::string&)
        {
            return rig;
        };
        services.constrain = [&](WorldState::Transaction&, const std::vector<RuntimePose>&, std::string& error)
        {
            middle = observer->CandidateSnapshot();
            if (fail)
            {
                error = "injected root collision failure";
                return false;
            }
            return true;
        };
        auto animation = std::make_unique<RuntimeAnimation>(std::filesystem::path{}, std::move(services));
        observer = animation.get();
        session.SetSystem(std::move(animation));
        std::string error;
        Require(session.Start(error) && observer->Snapshot().size() == 1, "runtime-animation-starts-without-renderer");
        const auto entity = *session.Read().Resolve(actor.id);
        const auto initial = observer->Snapshot()[0];
        Engine::CpuProfiler::Take();
        Require(session.Tick(.1, {}, false, error), "runtime-animation-ticks-typed-world");
        const auto moved = session.Read().Transform(entity)->position[0];
        Require(moved > .09f && moved < .11f && initial.state->time == 0,
                "runtime-animation-applies-root-motion-and-keeps-old-pose-generation");
        Require(events == 0 && session.Tick(.1, {}, false, error) && events == 1,
                "runtime-animation-events-deliver-on-next-update-once");
        Require(!middle.empty() && middle[0].state->rootDelta.position[0] > .09f &&
                    observer->Snapshot()[0].state->rootDelta.position[0] == 0,
                "runtime-animation-candidate-lease-is-immutable-across-late-evaluation");
        const auto pose = observer->Snapshot()[0];
        const auto transform = session.Read().Transform(entity);
        session.TakeChanges();
        fail = true;
        Require(!session.Tick(.1, {}, false, error) && observer->Snapshot()[0].state == pose.state &&
                    session.Read().Transform(entity) == transform && session.TakeChanges().Empty(),
                "runtime-animation-root-query-failure-restores-clock-pose-and-transform");
        fail = false;
        Require(session.Tick(.1, {}, false, error) && observer->Snapshot()[0].state->time > pose.state->time,
                "runtime-animation-retries-from-published-clock");
        RuntimeInput disable;
        disable.pressed["disable"] = true;
        const auto beforeDisable = session.Read().Transform(entity);
        Require(session.Tick(.1, disable, false, error) && session.Read().Transform(entity) == beforeDisable &&
                    session.Tick(.1, {}, false, error) && session.Read().Transform(entity) == beforeDisable,
                "runtime-animation-root-override-persists-without-changing-definition");
        bool exported = false;
        for (const auto& sample : Engine::CpuProfiler::Take())
            exported |= sample.name == "Runtime authoring export";
        Require(!exported && definition->Source().objects[0].position[0] == 0,
                "runtime-animation-does-not-copy-or-mutate-scene-layout-per-tick");
        const auto oldRig = observer->Snapshot()[0];
        Require(session.ReloadAssets(error) && observer->Snapshot()[0].state->time == oldRig.state->time,
                "runtime-animation-unchanged-artifact-keeps-clock");
        rig = std::make_shared<Engine::SkeletonData>(*rig);
        rig->nodes[0].name = "changed-root";
        Require(!session.ReloadAssets(error) && observer->Snapshot()[0].rig == oldRig.rig &&
                    observer->Snapshot()[0].state->time == oldRig.state->time,
                "runtime-animation-invalid-reimport-keeps-clock-and-rig");
        rig->nodes[0].name = "root";
        Require(session.ReloadAssets(error) && observer->Snapshot()[0].rig == rig &&
                    observer->Snapshot()[0].state->time == 0 && oldRig.rig != rig,
                "runtime-animation-reimport-publishes-new-rig-with-old-lease-valid");
        const auto priorInstance = observer->Snapshot()[0].component;
        auto without = *session.Read().Object(entity);
        without.animator.reset();
        Require(session.Edit(ComponentsEdit{{without}}, error) && observer->Snapshot().empty() &&
                    session.Edit(ComponentsEdit{{actor}}, error) && observer->Snapshot().size() == 1 &&
                    observer->Snapshot()[0].component != priorInstance && observer->Snapshot()[0].state->time == 0,
                "runtime-animation-component-recreation-gets-new-clock-and-generation");
    }
    void DuplicatePosePublication()
    {
        auto rig = std::make_shared<Engine::SkeletonData>();
        DirectX::XMStoreFloat4x4(&rig->inverseRoot, DirectX::XMMatrixIdentity());
        Engine::SkeletonNode bone;
        bone.name = "root";
        rig->nodes.push_back(bone);
        Engine::SkeletalClip clip;
        clip.name = "Walk";
        clip.duration = 1;
        clip.tracks[0].positions = {{0, {0, 0, 0}}, {1, {1, 0, 0}}};
        rig->clips.push_back(clip);
        ScenePlacement actor;
        actor.id = "actor";
        actor.meshRenderer = MeshRendererComponent{};
        actor.meshRenderer->model = "Assets/Models/actor.gltf";
        actor.animator = AnimatorComponent{};
        actor.animator->states = {{"Idle", "Walk", 1, true}};
        ScriptComponent script;
        script.behaviour = "Duplicate";
        actor.scripts.push_back(script);
        SceneLayout source;
        source.objects = {actor};
        auto definitions = std::make_shared<RuntimeScriptDefinitions>();
        (*definitions)["Duplicate"].update = [](RuntimeScriptContext& context)
        {
            if (context.Pressed("clone"))
                context.scene.Instantiate(context.entity, {0, 0, 6});
        };
        RuntimeSession session(WorldDefinition::Compile(source), definitions);
        RuntimeAnimationServices services;
        bool failImport = false;
        services.load = [rig, &failImport](const auto&,
                                           std::string& error) -> std::shared_ptr<const Engine::SkeletonData>
        {
            if (failImport)
            {
                error = "injected duplicate resource failure";
                return {};
            }
            return rig;
        };
        auto animation = std::make_unique<RuntimeAnimation>(std::filesystem::path{}, std::move(services));
        auto* observer = animation.get();
        session.SetSystem(std::move(animation));
        std::string error;
        Require(session.Start(error) && session.Tick(.05, {}, false, error),
                "runtime-animation-duplicate-source-advances-before-copy");
        const auto original = observer->Snapshot()[0];
        const auto originalTime = original.state->time;
        session.TakeChanges();
        failImport = true;
        Require(!session.Edit(DuplicateEdit{{"actor"}, {0, 0, 3}}, error) && session.Read().Entities().size() == 1 &&
                    observer->Snapshot()[0].state == original.state && session.TakeChanges().Empty(),
                "runtime-animation-duplicate-failure-keeps-source-state-and-notifications");
        failImport = false;
        Require(session.Edit(DuplicateEdit{{"actor"}, {0, 0, 3}}, error) && observer->Snapshot().size() == 2,
                "runtime-animation-host-duplicate-prepares-independent-instance");
        const auto copied = observer->Snapshot()[1];
        Require(session.Read().ObjectId(copied.entity) == "object-1",
                "runtime-animation-duplicate-retry-does-not-consume-reserved-id");
        Require(copied.state->time == original.state->time &&
                    copied.state->normalizedTime == original.state->normalizedTime &&
                    copied.state->pose.size() == original.state->pose.size() &&
                    copied.state->pose[0].position == original.state->pose[0].position &&
                    copied.state != original.state && copied.component != original.component,
                "runtime-animation-host-duplicate-preserves-clock-phase-pose-with-new-generation");
        RuntimeInput clone;
        clone.pressed["clone"] = true;
        Require(session.Tick(.05, clone, false, error) && observer->Snapshot().size() == 4,
                "runtime-animation-script-duplicates-propagate-through-phase-end");
        const auto advanced = observer->Snapshot()[0];
        Require(std::abs(advanced.state->time - .1) < .00001, "runtime-animation-duplicate-source-advances-once");
        for (const auto& pose : observer->Snapshot())
            Require(pose.state->time == advanced.state->time &&
                        pose.state->normalizedTime == advanced.state->normalizedTime,
                    "runtime-animation-script-duplicate-keeps-source-phase");
        Require(original.state->time == originalTime && originalTime > 0 && source.objects.size() == 1,
                "runtime-animation-duplicate-keeps-old-lease-and-authoring-source");
    }

    void PhysicalPosePublication()
    {
        auto rig = std::make_shared<Engine::SkeletonData>();
        DirectX::XMStoreFloat4x4(&rig->inverseRoot, DirectX::XMMatrixIdentity());
        Engine::SkeletonNode bone;
        bone.name = "root";
        rig->nodes.push_back(bone);
        ScenePlacement actor, body;
        actor.id = "actor";
        actor.meshRenderer = MeshRendererComponent{};
        actor.meshRenderer->model = "Assets/Models/actor.gltf";
        actor.animator = AnimatorComponent{};
        actor.ragdoll = RagdollComponent{};
        actor.ragdoll->active = true;
        actor.ragdoll->bones.push_back({"root", "body"});
        body.id = "body";
        body.position[1] = 2;
        body.boxCollider = BoxColliderComponent{};
        body.rigidBody = RigidBodyComponent{};
        SceneLayout source;
        source.objects = {actor, body};
        RuntimeSession session(WorldDefinition::Compile(source));
        RuntimeAnimationServices services;
        services.load = [rig](const auto&, std::string&)
        {
            return rig;
        };
        auto animation = std::make_unique<RuntimeAnimation>(std::filesystem::path{}, std::move(services));
        auto* observer = animation.get();
        session.SetSystem(std::move(animation));
        std::string error;
        Require(session.Start(error) && observer->Snapshot()[0].state->pose[0].position[1] == 2,
                "runtime-animation-start-publishes-physical-pose");
        const auto initial = observer->Snapshot()[0];
        Require(session.Tick(.1, {}, false, error) && observer->Snapshot()[0].state->pose[0].position[1] == 2,
                "runtime-animation-prepare-retains-ragdoll-after-update-and-late");
        Require(session.Edit(TransformEdit{"body", {{0, 3, 0}, {}, {1, 1, 1}}}, error) &&
                    observer->Snapshot()[0].state->pose[0].position[1] == 3 && initial.state->pose[0].position[1] == 2,
                "runtime-animation-edit-refreshes-physical-pose-with-old-generation-valid");
        Require(source.objects[1].position[1] == 2, "runtime-animation-physical-pose-does-not-change-authoring-source");
    }
}
int main()
{
    try
    {
        Validate();
        PhysicalPosePublication();
        DuplicatePosePublication();
        std::cout << "Runtime animation contracts: " << contracts << ", failures: 0\n";
        return 0;
    }
    catch (const std::exception& failure)
    {
        std::cerr << "FAIL: " << failure.what() << '\n';
        return 1;
    }
}
