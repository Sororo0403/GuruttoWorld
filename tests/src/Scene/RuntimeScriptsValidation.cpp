#include <SceneRuntime/Scripts/RuntimeScripts.h>
#include <Engine/Core/Profiler.h>
#include "../Support/AllocationProbe.h"
#include <iostream>
#include <cstdio>
#include <cstdlib>

#if defined(__d3d12_h__) || defined(_WINDOWS_)
#error Script runtime must not include platform or rendering APIs
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
    SceneLayout Source(const std::string& behavior)
    {
        ScenePlacement object;
        object.id = "owner";
        object.name = "Owner";
        object.material = MaterialComponent{};
        ScriptComponent script;
        script.id = "script-one";
        script.behaviour = behavior;
        object.scripts.push_back(script);
        SceneLayout source;
        source.objects.push_back(object);
        return source;
    }
    bool Tick(RuntimeScripts& scripts, WorldState& world, double seconds, std::string& error)
    {
        auto transaction = world.BeginTransaction();
        auto frame = scripts.BeginFrame(transaction);
        if (!frame.Run(ScriptPhase::Update, seconds, error) || !frame.Prepare(error) || !transaction.Prepare(error))
            return false;
        bool committed;
        TestSupport::AllocationCounts allocations;
        {
            TestSupport::AllocationProbe probe(true);
            committed = transaction.Commit() && frame.Commit();
            allocations = probe.Counts();
        }
        Require(committed && allocations.count == 0, "script-and-world-commit-without-allocation");
        frame.FinishCallbacks();
        return true;
    }
    void BuiltinContracts()
    {
        auto source = Source("Bob");
        const auto definition = WorldDefinition::Compile(source);
        WorldState world(definition);
        RuntimeScripts scripts;
        std::string error;
        Require(scripts.Validate(world.Read(), error), "script-validates-without-D3D");
        const auto entity = *world.Read().Resolve("owner");
        Engine::CpuProfiler::Take();
        Require(Tick(scripts, world, 1.0 / 60, error) && world.Read().Transform(entity)->position[1] > .05f,
                "script-builtin-updates-typed-world");
        Require(definition->Source().Serialize() == source.Serialize(), "script-does-not-mutate-world-definition");
        bool exported = false;
        for (const auto& phase : Engine::CpuProfiler::Take())
            exported |= phase.name == "Runtime authoring export";
        Require(!exported, "script-ordinary-tick-does-not-export-layout");
        const auto first = world.Read().Transform(entity)->position[1];
        Require(Tick(scripts, world, 1.0 / 60, error) && world.Read().Transform(entity)->position[1] > first,
                "script-retains-instance-state");
        {
            auto transaction = world.BeginTransaction();
            auto frame = scripts.BeginFrame(transaction);
            Require(frame.Pause(true, error) && frame.Prepare(error) && transaction.Prepare(error) &&
                        transaction.Commit() && frame.Commit(),
                    "script-pause-is-transactional");
        }
        const auto paused = world.Read().Transform(entity);
        Require(Tick(scripts, world, .1, error) && world.Read().Transform(entity) == paused,
                "script-paused-update-retains-transform");
    }
    void FailureContracts()
    {
        int starts = 0, stops = 0, updates = 0, events = 0;
        bool fail = false;
        auto definitions = std::make_shared<RuntimeScriptDefinitions>();
        auto& definition = (*definitions)["Transactional"];
        definition.start = [&](RuntimeScriptContext& context)
        {
            ++starts;
            context.state["count"] = 0;
        };
        definition.update = [&](RuntimeScriptContext& context)
        {
            ++updates;
            context.state["count"] += 1;
            auto transform = *context.Read().Transform(context.entity);
            transform.position[0] = context.state["count"];
            Require(context.SetLocalTransform(transform).succeeded, "script-candidate-transform-request");
            context.scene.SetUiValue("count", context.state["count"]);
            context.scene.Emit({"event", "owner", "owner"});
            if (fail)
                throw std::runtime_error("injected script failure");
        };
        definition.onEvent = [&](RuntimeScriptContext&)
        {
            ++events;
        };
        definition.stop = [&](const RuntimeScriptStopContext& context)
        {
            ++stops;
            Require(context.object.id == "owner" && context.state.contains("count"), "script-stop-owns-values");
        };
        WorldState world(WorldDefinition::Compile(Source("Transactional")));
        RuntimeScripts scripts(definitions);
        const auto entity = *world.Read().Resolve("owner");
        std::string error;
        Require(Tick(scripts, world, .1, error) && starts == 1 && events == 0, "script-start-and-next-frame-event");
        world.TakeChanges();
        fail = true;
        Require(!Tick(scripts, world, .1, error) && world.Read().Transform(entity)->position[0] == 1 &&
                    world.TakeChanges().Empty() && stops == 0,
                "script-failed-update-retains-published-state-and-notices");
        fail = false;
        Require(Tick(scripts, world, .1, error) && world.Read().Transform(entity)->position[0] == 2 && starts == 1,
                "script-failed-update-does-not-advance-instance-state");
        Require(events == 2, "script-failed-event-consumption-is-retried");
        auto effects = scripts.TakeEffects();
        Require(effects.ui.values.at("count") == 2 && scripts.TakeEffects().ui.values.empty(),
                "script-effects-drain-once");
        {
            auto transaction = world.BeginTransaction();
            auto frame = scripts.BeginFrame(transaction);
            Require(!scripts.QueueEvent({"reentrant"}, error), "script-active-frame-rejects-event-reentry");
        }
        scripts.Stop(world);
        Require(stops == 1, "script-stop-runs-once-after-commit");
        scripts.Stop(world);
        Require(stops == 1 && updates == 3, "script-repeated-stop-does-not-repeat-callback");

        WorldState failingWorld(WorldDefinition::Compile(Source("Transactional")));
        RuntimeScripts failingScripts(definitions);
        fail = true;
        Require(!Tick(failingScripts, failingWorld, .1, error) && starts == 2 && stops == 2,
                "script-failed-first-update-cleans-new-instance");
        fail = false;
        Require(Tick(failingScripts, failingWorld, .1, error) && starts == 3, "script-aborted-start-restarts-on-retry");
        failingScripts.Stop(failingWorld);
    }
    void PhaseContracts()
    {
        auto definitions = std::make_shared<RuntimeScriptDefinitions>();
        int fixed = 0, late = 0;
        auto& definition = (*definitions)["Phases"];
        definition.update = [](RuntimeScriptContext& context)
        {
            auto material = context.Read().Component<MaterialComponent>(context.entity);
            material->roughness = .2f;
            Require(context.SetComponent(material).succeeded &&
                        context.Read().Component<MaterialComponent>(context.entity)->roughness == .5f,
                    "script-component-visible-at-phase-end");
        };
        definition.fixedUpdate = [&](RuntimeScriptContext& context)
        {
            ++fixed;
            Require(context.Input("axis") == 1, "script-fixed-input-snapshot");
        };
        definition.lateUpdate = [&](RuntimeScriptContext& context)
        {
            ++late;
            Require(context.Read().Component<MaterialComponent>(context.entity)->roughness == .2f,
                    "script-late-sees-update-component");
        };
        WorldState world(WorldDefinition::Compile(Source("Phases")));
        RuntimeScripts scripts(definitions);
        std::string error;
        {
            auto transaction = world.BeginTransaction();
            auto frame = scripts.BeginFrame(transaction, {{"axis", 1.f}});
            Require(frame.Run(ScriptPhase::FixedUpdate, .02, error) && frame.Run(ScriptPhase::Update, .1, error) &&
                        frame.Run(ScriptPhase::LateUpdate, .1, error) && frame.Prepare(error) &&
                        transaction.Prepare(error) && transaction.Commit() && frame.Commit(),
                    "script-three-phases-share-one-transaction");
        }
        Require(fixed == 1 && late == 1, "script-fixed-and-late-run-once");
    }
    void StructureContracts()
    {
        auto definitions = std::make_shared<RuntimeScriptDefinitions>();
        int stops = 0;
        std::string spawned;
        auto& definition = (*definitions)["Structure"];
        definition.update = [&](RuntimeScriptContext& context)
        {
            ScenePlacement child;
            child.name = "Created";
            child.parentId = "owner";
            spawned = context.scene.Spawn(child);
            Require(!context.scene.Find(spawned), "script-spawn-is-hidden-until-phase-end");
            auto pose = *context.Read().Transform(context.entity);
            pose.position[0] = 2;
            Require(context.SetLocalTransform(pose).succeeded, "script-structural-queue-retains-later-transform");
        };
        definition.lateUpdate = [&](RuntimeScriptContext& context)
        {
            Require(context.scene.Find(spawned).has_value(), "script-late-sees-phase-end-spawn");
        };
        definition.stop = [&](const RuntimeScriptStopContext& context)
        {
            ++stops;
            Require(context.object.id == "owner" && context.object.position[0] == 2,
                    "script-destroy-retains-final-stop-owner");
        };
        WorldState world(WorldDefinition::Compile(Source("Structure")));
        RuntimeScripts scripts(definitions);
        std::string error;
        {
            auto transaction = world.BeginTransaction();
            auto frame = scripts.BeginFrame(transaction);
            Require(frame.Run(ScriptPhase::Update, .01, error) && frame.Run(ScriptPhase::LateUpdate, .01, error) &&
                        frame.Prepare(error) && transaction.Prepare(error) && transaction.Commit() && frame.Commit(),
                    "script-spawn-and-late-commit-together");
            frame.FinishCallbacks();
        }
        const auto entity = *world.Read().Resolve("owner");
        Require(world.Read().Transform(entity)->position[0] == 2 && world.Read().Resolve(spawned).has_value(),
                "script-spawn-does-not-lose-numeric-updates");
        {
            auto transaction = world.BeginTransaction();
            auto frame = scripts.BeginFrame(transaction);
            Require(frame.Edit(RemoveObjectsEdit{{"owner"}}, error) && frame.Start(error) && frame.Prepare(error) &&
                        transaction.Prepare(error),
                    "script-explicit-destroy-prepares-stop");
            Require(stops == 0, "script-destroy-does-not-stop-before-commit");
            Require(transaction.Commit() && frame.Commit(), "script-explicit-destroy-commits");
            frame.FinishCallbacks();
            frame.FinishCallbacks();
        }
        Require(stops == 1 && !world.Read().Alive(entity), "script-destroy-stop-is-once-after-publication");
    }
    void RespawnContracts()
    {
        auto definitions = std::make_shared<RuntimeScriptDefinitions>();
        int stops = 0;
        auto source = Source("Respawn");
        auto replacement = source.objects.front();
        replacement.position[0] = 4;
        auto& definition = (*definitions)["Respawn"];
        definition.update = [&](RuntimeScriptContext& context)
        {
            context.scene.Destroy(context.entity);
            Require(context.scene.Spawn(replacement) == "owner", "script-same-phase-respawn-reuses-saved-id");
        };
        definition.stop = [&](const RuntimeScriptStopContext& context)
        {
            ++stops;
            Require(context.object.id == "owner" && context.object.position[0] == 0,
                    "script-respawn-stop-keeps-retired-owner");
        };
        WorldState world(WorldDefinition::Compile(source));
        RuntimeScripts scripts(definitions);
        std::string startError;
        {
            auto transaction = world.BeginTransaction();
            auto frame = scripts.BeginFrame(transaction);
            Require(frame.Start(startError) && frame.Prepare(startError) && transaction.Prepare(startError) &&
                        transaction.Commit() && frame.Commit(),
                    "script-respawn-starts-published-owner");
            frame.FinishCallbacks();
        }
        const auto old = *world.Read().Resolve("owner");
        const auto oldScript = *world.Read().RepeatedInstance<ScriptComponent>(old, replacement.scripts.front().id);
        std::string error;
        for (bool commit : {false, true})
        {
            auto transaction = world.BeginTransaction();
            auto frame = scripts.BeginFrame(transaction);
            Require(frame.Run(ScriptPhase::Update, .01, error) && frame.Prepare(error) && transaction.Prepare(error),
                    "script-respawn-prepares-transaction");
            const auto candidate = *transaction.Read().Resolve("owner");
            Require(candidate != old && !transaction.Read().Alive(old) && !transaction.Read().Alive(oldScript) &&
                        transaction.Read().Transform(candidate)->position[0] == 4,
                    "script-same-phase-respawn-rejects-retired-handles");
            Require(world.Read().Alive(old) && world.Read().Alive(oldScript) && stops == 0,
                    "script-respawn-preparation-keeps-published-lifetime");
            if (commit)
            {
                Require(transaction.Commit() && frame.Commit(), "script-respawn-retry-commits");
                frame.FinishCallbacks();
                frame.FinishCallbacks();
            }
        }
        Require(stops == 1 && !world.Read().Alive(old) && !world.Read().Alive(oldScript) &&
                    world.Read().Resolve("owner").has_value(),
                "script-respawn-publishes-new-lifetime-and-stops-once");
    }
    void ValidateBorrowedData()
    {
        ScriptValue::Object data;
        ScriptValue::Array values;
        for (unsigned int index = 0; index < 128; ++index)
            values.push_back(ScriptValue{static_cast<float>(index)});
        data["values"] = ScriptValue{std::move(values)};
        ScriptValue owned{data};
        size_t expected = 4096, actual = 4096;
        owned.Validate(0, expected);
        TestSupport::AllocationCounts counts;
        {
            TestSupport::AllocationProbe probe(true);
            ScriptValue::ValidateObject(data, 0, actual);
            counts = probe.Counts();
        }
        Require(actual == expected && counts.count == 0,
                "runtime-borrowed-script-data-validation-keeps-budget-without-copying");
        bool rejected = false;
        auto invalid = data;
        invalid[""] = ScriptValue{1.f};
        try
        {
            size_t remaining = 4096;
            ScriptValue::ValidateObject(invalid, 0, remaining);
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        Require(rejected, "runtime-borrowed-script-data-keeps-name-validation");
    }
}

int main()
{
    std::set_terminate(
        []
        {
            std::fputs("FAIL: unexpected terminate\n", stderr);
            std::_Exit(86);
        });
    try
    {
        BuiltinContracts();
        FailureContracts();
        PhaseContracts();
        StructureContracts();
        RespawnContracts();
        ValidateBorrowedData();
        std::cout << "Runtime script contracts: " << contracts << ", failures: 0\n";
        return 0;
    }
    catch (const std::exception& failure)
    {
        std::cerr << "FAIL: " << failure.what() << '\n';
        return 1;
    }
}
