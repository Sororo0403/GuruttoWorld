#include <SceneRuntime/Scene/RuntimeSession.h>
#include <Engine/Core/Profiler.h>
#include <iostream>
#include <limits>

#if defined(__d3d12_h__) || defined(_WINDOWS_)
#error Runtime session must not depend on platform or rendering APIs
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
    struct System final : RuntimeSystem
    {
        bool failFixed = false, failUpdate = false, failPrepare = false;
        int fixed = 0, updated = 0, published = 0, finished = 0;
        bool sawJump = false, sawPressed = false;
        RuntimeSession* session = nullptr;
        struct Candidate final : Frame
        {
            System& owner;
            WorldState::Transaction& transaction;
            int fixed = 0, updated = 0;
            bool jump = false, pressed = false;
            Candidate(System& value, WorldState::Transaction& tx) : owner(value), transaction(tx) {}
            bool Fixed(double seconds, const RuntimeInput& input, const RuntimeScriptEffects&, std::string&) override
            {
                Require(seconds > 0, "session-fixed-positive-step");
                ++fixed;
                jump |= input.jump;
                pressed |= input.pressed.contains("jump") && input.pressed.at("jump");
                return !owner.failFixed;
            }
            bool Update(double, const RuntimeScriptEffects&, std::string&) override
            {
                ++updated;
                return !owner.failUpdate;
            }
            bool Prepare(const SceneChangeSet&, const RuntimeScriptEffects&, std::string&) override
            {
                if (owner.session)
                {
                    std::string error;
                    Require(!owner.session->Tick(.01, {}, true, error) && owner.session->TakeChanges().Empty(),
                            "session-preparation-rejects-reentry-and-drain");
                }
                return !owner.failPrepare;
            }
            void Commit() noexcept override
            {
                owner.fixed += fixed;
                owner.updated += updated;
                ++owner.published;
                owner.sawJump |= jump;
                owner.sawPressed |= pressed;
            }
            void Finish() noexcept override
            {
                ++owner.finished;
            }
        };
        std::unique_ptr<Frame> Begin(WorldState::Transaction& transaction) override
        {
            return std::make_unique<Candidate>(*this, transaction);
        }
        RuntimeScriptQueries Queries() const override
        {
            return {};
        }
    };
    void Validate()
    {
        SceneLayout layout;
        ScenePlacement object;
        object.id = "owner";
        object.scripts.push_back(ScriptComponent{});
        layout.objects.push_back(object);
        const auto definition = WorldDefinition::Compile(layout);
        RuntimeSession session(definition);
        auto system = std::make_unique<System>();
        auto* observer = system.get();
        observer->session = &session;
        session.SetSystem(std::move(system));
        std::string error;
        Require(session.Start(error) && observer->published == 1 && observer->finished == 1,
                "session-start-prepares-and-publishes");
        const auto entity = *session.Read().Resolve("owner");
        const auto started = session.TakeChanges();
        Require(std::any_of(started.fields.begin(), started.fields.end(),
                            [](const auto& field)
                            {
                                return field.objectId == "owner" && field.componentId == "script" &&
                                       field.field == "runtime.instance";
                            }),
                "session-publishes-script-instance-by-component-ID");
        RuntimeInput input;
        input.jump = true;
        input.pressed["jump"] = true;
        const double step = layout.settings.fixedStep;
        Require(session.Tick(step * .4, input, true, error) && session.FixedTicks() == 0 && observer->fixed == 0,
                "session-short-frame-retains-fixed-input");
        const auto before = session.Read().Transform(entity);
        const auto interpolation = session.Interpolation();
        session.TakeChanges();
        observer->failPrepare = true;
        Require(!session.Tick(step, {}, true, error) && session.Read().Transform(entity) == before &&
                    session.FixedTicks() == 0 && session.Interpolation() == interpolation &&
                    session.TakeChanges().Empty(),
                "session-output-failure-restores-world-script-clock-and-notices");
        observer->failPrepare = false;
        Require(session.Tick(step, {}, true, error) && session.FixedTicks() == 1 && observer->sawJump &&
                    observer->sawPressed,
                "session-retry-consumes-retained-fixed-input-once");
        observer->sawJump = observer->sawPressed = false;
        Require(session.Tick(step, {}, true, error) && !observer->sawJump && !observer->sawPressed,
                "session-fixed-input-does-not-repeat");
        const auto ticks = session.FixedTicks();
        observer->failFixed = true;
        Require(!session.Tick(step, {}, true, error) && session.FixedTicks() == ticks,
                "session-fixed-failure-does-not-publish");
        observer->failFixed = false;
        observer->failUpdate = true;
        Require(!session.Tick(step, {}, true, error) && session.FixedTicks() == ticks,
                "session-update-failure-rolls-back-fixed-stage");
        observer->failUpdate = false;
        Require(session.Tick(1, {}, true, error) && session.FixedTicks() - ticks <= layout.settings.maximumFixedSteps &&
                    session.Interpolation() >= 0 && session.Interpolation() < 1,
                "session-caps-fixed-backlog");
        Require(!session.Tick(std::numeric_limits<double>::quiet_NaN(), {}, true, error),
                "session-rejects-invalid-time");
        const auto pose = session.Read().Transform(entity);
        observer->failPrepare = true;
        Require(!session.Edit(RemoveObjectsEdit{{"owner"}}, error) && session.Read().Alive(entity) &&
                    session.Read().Transform(entity) == pose,
                "session-failed-scene-edit-retains-running-world");
        observer->failPrepare = false;
        Require(session.Edit(RemoveObjectsEdit{{"owner"}}, error) && !session.Read().Alive(entity),
                "session-successful-destroy-retires-entity");
        Require(definition->Source().objects.size() == 1, "session-retains-original-definition");
        Require(observer->published == observer->finished, "session-finishes-only-published-output");
    }
    void ValidateLifecycle()
    {
        for (const bool destroy : {false, true})
        {
            int stops = 0;
            auto definitions = std::make_shared<RuntimeScriptDefinitions>();
            auto& definition = (*definitions)["SelfRemove"];
            definition.update = [destroy](RuntimeScriptContext& context)
            {
                if (destroy)
                    context.scene.Destroy(context.entity);
                else
                    Require(context.SetRepeated<ScriptComponent>({}).succeeded, "session-queues-script-removal");
                auto pose = *context.Read().Transform(context.entity);
                pose.position[0] = 4;
                Require(context.SetLocalTransform(pose).succeeded, "session-queued-destroy-keeps-callback-owner-alive");
            };
            definition.prepareStop = [](RuntimeScriptContext& context)
            {
                context.state["prepared"] = 1;
            };
            definition.stop = [&](const RuntimeScriptStopContext& context)
            {
                ++stops;
                Require(context.object.id == "owner" && context.object.position[0] == 4 &&
                            context.state.at("prepared") == 1,
                        "session-stop-retains-final-owner-and-prepared-state");
            };
            SceneLayout source;
            ScenePlacement owner;
            owner.id = "owner";
            ScriptComponent script;
            script.behaviour = "SelfRemove";
            owner.scripts.push_back(script);
            source.objects.push_back(owner);
            RuntimeSession session(WorldDefinition::Compile(source), definitions);
            std::string error;
            Require(session.Start(error) && session.Tick(.01, {}, false, error), "session-self-removal-succeeds");
            if (destroy)
                Require(stops == 1, "session-object-destroy-stops-at-phase-end");
            else
                Require(stops == 0 && session.Tick(.01, {}, false, error) && stops == 1,
                        "session-component-removal-stops-at-next-actual-phase");
        }
    }
    void ValidateHostOperations()
    {
        int starts = 0, updates = 0, fixed = 0;
        auto definitions = std::make_shared<RuntimeScriptDefinitions>();
        auto& script = (*definitions)["HostOperations"];
        script.start = [&](RuntimeScriptContext&)
        {
            ++starts;
        };
        script.update = [&](RuntimeScriptContext&)
        {
            ++updates;
        };
        script.fixedUpdate = [&](RuntimeScriptContext&)
        {
            ++fixed;
        };
        SceneLayout layout;
        ScenePlacement owner;
        owner.id = "owner";
        ScriptComponent component;
        component.behaviour = "HostOperations";
        owner.scripts.push_back(component);
        layout.objects.push_back(owner);
        RuntimeSession session(WorldDefinition::Compile(layout), definitions);
        auto system = std::make_unique<System>();
        auto* observer = system.get();
        session.SetSystem(std::move(system));
        std::string error;
        Require(session.Edit(TransformEdit{"owner", {{1, 0, 0}, {}, {1, 1, 1}}}, error) && starts == 0,
                "session-edit-before-play-defers-script-start");
        RuntimeScriptEffects effects;
        effects.ui.values["value"] = 2;
        observer->failPrepare = true;
        Require(!session.ApplyEffects(effects, error) && session.TakeEffects().ui.values.empty() && starts == 0,
                "session-host-effects-fail-without-publication-or-start");
        observer->failPrepare = false;
        Require(session.ApplyEffects(effects, error) && starts == 0 && observer->updated == 0,
                "session-host-effects-do-not-advance-variable-stage");
        RuntimeScriptEffects drained;
        Require(session.DrainEffects(drained) && drained.ui.values.at("value") == 2 &&
                    session.TakeEffects().ui.values.empty(),
                "session-host-effects-drain-once");
        Require(session.TickFixed(1.0 / 60.0, {}, error) && starts == 1 && fixed == 1 && updates == 0 &&
                    observer->updated == 0,
                "session-fixed-only-keeps-variable-callbacks-idle");
        Require(session.HasPhase(ScriptPhase::FixedUpdate) && session.Validate(error) && !session.Paused(),
                "session-host-queries-use-pinned-definitions");
        Require(session.Pause(true, error) && session.Paused() && session.Pause(false, error) && !session.Paused(),
                "session-host-pause-query-follows-commit");
        Require(session.Tick(.01, {}, false, error) && starts == 1 && updates == 1,
                "session-variable-resume-keeps-started-instance");
        Require(session.Edit(RemoveObjectsEdit{{"owner"}}, error) && !session.HasPhase(ScriptPhase::FixedUpdate),
                "session-host-phase-query-removes-dead-owner");
    }
    void ValidatePropertyAnimation()
    {
        SceneLayout layout;
        ScenePlacement object;
        object.id = "animated";
        object.pointLight.emplace();
        object.animation.emplace();
        AnimationTrack position;
        position.property = "position";
        position.clock = "time";
        position.easing = "linear";
        position.keys = {{0, {0, 0, 0, 0}}, {1, {2, 0, 0, 0}}};
        AnimationTrack light = position;
        light.property = "pointLight.intensity";
        light.keys = {{0, {5, 0, 0, 0}}, {1, {10, 0, 0, 0}}};
        object.animation->tracks = {position, light};
        int starts = 0;
        auto definitions = std::make_shared<RuntimeScriptDefinitions>();
        (*definitions)["DeferredAnimation"].start = [&](RuntimeScriptContext&)
        {
            ++starts;
        };
        ScriptComponent script;
        script.behaviour = "DeferredAnimation";
        object.scripts.push_back(script);
        layout.objects.push_back(object);
        const auto source = WorldDefinition::Compile(layout);
        RuntimeSession session(source, definitions);
        auto system = std::make_unique<System>();
        auto* observer = system.get();
        session.SetSystem(std::move(system));
        const auto entity = *session.Read().Resolve("animated");
        std::string error;
        Engine::CpuProfiler::Take();
        Require(session.Animate({{"time", .5f}}, error) && session.Read().Transform(entity)->position[0] == 1 &&
                    session.Read().ComponentLease<PointLightComponent>(entity)->intensity == 7.5f && starts == 0 &&
                    session.FixedTicks() == 0,
                "session-property-animation-publishes-transform-and-component-without-starting-scripts-or-clock");
        const auto scopes = Engine::CpuProfiler::Take();
        Require(std::none_of(scopes.begin(), scopes.end(),
                             [](const auto& scope)
                             {
                                 return scope.name == "Runtime authoring export";
                             }) &&
                    source->Source().objects[0].position[0] == 0,
                "session-property-animation-keeps-source-immutable-without-whole-layout-export");
        session.TakeChanges();
        observer->failPrepare = true;
        Require(!session.Animate({{"time", .75f}}, error) && session.Read().Transform(entity)->position[0] == 1 &&
                    session.TakeChanges().Empty(),
                "session-property-animation-resource-rejection-retains-prior-values-and-notices");
        const auto publications = observer->published;
        Require(session.Animate({{"time", .5f}}, error) && observer->published == publications &&
                    session.TakeChanges().Empty(),
                "session-property-animation-same-values-skip-resource-preparation");
        observer->failPrepare = false;
        Require(session.Animate({{"time", 1.f}}, error) && session.Read().Transform(entity)->position[0] == 2 &&
                    session.Read().ComponentLease<PointLightComponent>(entity)->intensity == 10,
                "session-property-animation-retries-from-published-values");
        session.TakeChanges();
        Require(!session.Animate({{"time", std::numeric_limits<float>::quiet_NaN()}}, error) &&
                    session.Read().Transform(entity)->position[0] == 2 && session.TakeChanges().Empty(),
                "session-property-animation-rejects-nonfinite-clock-without-partial-publication");
        Require(session.Start(error) && starts == 1, "session-property-animation-keeps-explicit-script-start");
    }
    void ValidateDefaultClock()
    {
        SceneLayout layout;
        RuntimeSession session(WorldDefinition::Compile(layout));
        std::string error;
        Require(session.Tick(1.0 / 60.0, {}, true, error) && session.FixedTicks() == 1 && session.Interpolation() == 0,
                "session-default-step-preserves-double-precision-sixtieth");
    }
}
int main()
{
    try
    {
        Validate();
        ValidateLifecycle();
        ValidateDefaultClock();
        ValidateHostOperations();
        ValidatePropertyAnimation();
        std::cout << "Runtime session contracts: " << contracts << ", failures: 0\n";
        return 0;
    }
    catch (const std::exception& failure)
    {
        std::cerr << "FAIL: " << failure.what() << '\n';
        return 1;
    }
}
