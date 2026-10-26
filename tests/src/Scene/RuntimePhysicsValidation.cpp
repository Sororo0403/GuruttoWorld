#include <SceneRuntime/Scene/RuntimePipeline.h>
#include <SceneRuntime/Physics/RuntimePhysics.h>
#include <Engine/Core/Profiler.h>
#include <iostream>

#if defined(__d3d12_h__) || defined(_WINDOWS_)
#error Runtime simulation must not include platform or rendering APIs
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
    struct Output final : RuntimeSystem
    {
        bool fail = false;
        struct Candidate final : Frame
        {
            Output& owner;
            explicit Candidate(Output& value) : owner(value) {}
            bool Fixed(double, const RuntimeInput&, const RuntimeScriptEffects&, std::string&) override
            {
                return true;
            }
            bool Update(double, const RuntimeScriptEffects&, std::string&) override
            {
                return true;
            }
            bool Prepare(const SceneChangeSet&, const RuntimeScriptEffects&, std::string& error) override
            {
                if (owner.fail)
                {
                    error = "injected output failure";
                    return false;
                }
                return true;
            }
            void Commit() noexcept override {}
            void Finish() noexcept override {}
        };
        std::unique_ptr<Frame> Begin(WorldState::Transaction&) override
        {
            return std::make_unique<Candidate>(*this);
        }
        RuntimeScriptQueries Queries() const override
        {
            return {};
        }
    };
    Output* Attach(RuntimeSession& session, RuntimeScriptQueries* queries = nullptr)
    {
        std::vector<std::unique_ptr<RuntimeSystem>> systems;
        auto physics = std::make_unique<RuntimePhysics>(std::filesystem::path{});
        if (queries)
            *queries = physics->Queries();
        systems.push_back(std::move(physics));
        auto output = std::make_unique<Output>();
        auto* observer = output.get();
        systems.push_back(std::move(output));
        session.SetSystem(std::make_unique<RuntimePipeline>(std::move(systems)));
        return observer;
    }
    void Validate()
    {
        SceneLayout layout;
        ScenePlacement floor, body;
        floor.id = "floor";
        floor.position = {0, -1, 0};
        floor.boxCollider = BoxColliderComponent{};
        floor.boxCollider->size = {20, 1, 20};
        body.id = "body";
        body.position = {0, 3, 0};
        body.boxCollider = BoxColliderComponent{};
        body.rigidBody = RigidBodyComponent{};
        layout.objects = {floor, body};
        const auto definition = WorldDefinition::Compile(layout);
        RuntimeSession first(definition), reference(definition);
        RuntimeScriptQueries queries;
        auto* output = Attach(first, &queries);
        Attach(reference);
        std::string error;
        Require(first.Start(error) && reference.Start(error), "typed-physics-starts-without-renderer");
        Require(queries.raycast({0, 5, 0}, {0, -1, 0}, 10, 0xffffffffu, false, {})->object == body.id,
                "typed-physics-start-publishes-query-world");
        const auto actor = *first.Read().Resolve(body.id), expected = *reference.Read().Resolve(body.id);
        const double step = static_cast<double>(layout.settings.fixedStep);
        Engine::CpuProfiler::Take();
        for (size_t tick = 0; tick < 4; ++tick)
            Require(first.Tick(step, {}, true, error) && reference.Tick(step, {}, true, error),
                    "typed-physics-fixed-tick");
        Require(first.Read().Transform(actor)->position[1] < 3, "typed-physics-applies-gravity-to-runtime-values");
        const auto before = first.Read().Transform(actor);
        const auto ticks = first.FixedTicks();
        first.TakeChanges();
        output->fail = true;
        Require(!first.Tick(step, {}, true, error) && first.Read().Transform(actor) == before &&
                    first.FixedTicks() == ticks && first.TakeChanges().Empty(),
                "typed-physics-output-failure-restores-entire-step");
        output->fail = false;
        Require(first.Tick(step, {}, true, error) && reference.Tick(step, {}, true, error) &&
                    first.Read().Transform(actor) == reference.Read().Transform(expected),
                "typed-physics-retry-matches-uninterrupted-world");
        bool exported = false;
        for (const auto& sample : Engine::CpuProfiler::Take())
            exported |= sample.name == "Runtime authoring export";
        Require(!exported, "typed-physics-never-exports-scene-layout-per-tick");
        Require(definition->Source().objects[1].position[1] == 3, "typed-physics-retains-immutable-definition");
        RuntimeSession fresh(definition);
        Attach(fresh);
        Require(fresh.Start(error) && first.Edit(RemoveObjectsEdit{{body.id}}, error) &&
                    first.Edit(AddObjectEdit{body}, error),
                "typed-physics-recreates-same-saved-ID");
        const auto recreated = *first.Read().Resolve(body.id), initial = *fresh.Read().Resolve(body.id);
        Require(recreated != actor && first.Tick(step, {}, true, error) && fresh.Tick(step, {}, true, error) &&
                    first.Read().Transform(recreated) == fresh.Read().Transform(initial),
                "typed-physics-new-entity-does-not-inherit-old-velocity");
        auto withoutBody = *first.Read().Object(recreated);
        withoutBody.rigidBody.reset();
        Require(first.Edit(ComponentsEdit{{withoutBody}}, error) && first.Edit(ComponentsEdit{{body}}, error) &&
                    first.Edit(TransformEdit{body.id, {body.position, body.rotation, body.scale}}, error),
                "typed-physics-recreates-same-component-ID");
        RuntimeSession componentReference(definition);
        Attach(componentReference);
        Require(componentReference.Start(error) && first.Tick(step, {}, true, error) &&
                    componentReference.Tick(step, {}, true, error) &&
                    first.Read().Transform(recreated) ==
                        componentReference.Read().Transform(*componentReference.Read().Resolve(body.id)),
                "typed-physics-new-component-does-not-inherit-old-velocity");
        for (size_t tick = 0; tick < 90; ++tick)
            if (!first.Tick(step, {}, true, error))
                throw std::runtime_error(error);
        const auto settled = first.Read().Transform(recreated)->position[1];
        Require(settled > -.05f && settled < .15f, "typed-physics-body-settles-on-floor");
        Require(first.TakeChanges().fields.size() > 0, "typed-physics-publishes-runtime-notices");
        Require(first.Edit(RemoveObjectsEdit{{body.id}}, error) &&
                    queries.raycast({0, 5, 0}, {0, -1, 0}, 10, 0xffffffffu, false, {})->object == floor.id,
                "typed-physics-destroy-removes-query-body-before-next-tick");
        auto definitions = std::make_shared<RuntimeScriptDefinitions>();
        (*definitions)["Impulse"].update = [](RuntimeScriptContext& context)
        {
            if (context.Pressed("kick"))
                context.scene.AddImpulse(context.entity, {10, 0, 0});
        };
        ScriptComponent impulseScript;
        impulseScript.behaviour = "Impulse";
        body.scripts = {impulseScript};
        layout.objects[1] = body;
        const auto impulseDefinition = WorldDefinition::Compile(layout);
        RuntimeSession queued(impulseDefinition, definitions), clean(impulseDefinition, definitions);
        Attach(queued);
        Attach(clean);
        RuntimeInput kick;
        kick.pressed["kick"] = true;
        Require(queued.Start(error) && clean.Start(error) && queued.Tick(step * .1, kick, true, error),
                "typed-physics-queues-impulse-without-fixed-step");
        Require(queued.Edit(RemoveObjectsEdit{{body.id}}, error) && queued.Edit(AddObjectEdit{body}, error) &&
                    queued.Tick(step, {}, true, error) && clean.Tick(step, {}, true, error) &&
                    queued.Read().Transform(*queued.Read().Resolve(body.id)) ==
                        clean.Read().Transform(*clean.Read().Resolve(body.id)),
                "typed-physics-recreated-body-does-not-inherit-queued-impulse");
    }
}
int main()
{
    try
    {
        Validate();
        std::cout << "Runtime physics contracts: " << contracts << ", failures: 0\n";
        return 0;
    }
    catch (const std::exception& failure)
    {
        std::cerr << "FAIL: " << failure.what() << '\n';
        return 1;
    }
}
