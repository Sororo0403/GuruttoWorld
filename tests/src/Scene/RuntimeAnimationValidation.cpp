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
        const auto priorInstance = observer->Snapshot()[0].component;
        auto without = *session.Read().Object(entity);
        without.animator.reset();
        Require(session.Edit(ComponentsEdit{{without}}, error) && observer->Snapshot().empty() &&
                    session.Edit(ComponentsEdit{{actor}}, error) && observer->Snapshot().size() == 1 &&
                    observer->Snapshot()[0].component != priorInstance && observer->Snapshot()[0].state->time == 0,
                "runtime-animation-component-recreation-gets-new-clock-and-generation");
    }
}
int main()
{
    try
    {
        Validate();
        std::cout << "Runtime animation contracts: " << contracts << ", failures: 0\n";
        return 0;
    }
    catch (const std::exception& failure)
    {
        std::cerr << "FAIL: " << failure.what() << '\n';
        return 1;
    }
}
