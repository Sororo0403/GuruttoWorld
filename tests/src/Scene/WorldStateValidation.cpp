#include <SceneRuntime/Scene/WorldState.h>
#include <SceneRuntime/Scene/WorldStateReadAdapter.h>
#include <Engine/Core/Profiler.h>
#include "../Support/AllocationProbe.h"
#include <iostream>
#include <cstdio>
#include <cstdlib>

#if defined(__d3d12_h__) || defined(_WINDOWS_)
#error Runtime storage must not include platform or rendering APIs
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
        ScenePlacement root, child;
        root.id = "root";
        root.name = "Root";
        root.position = {2, 0, 0};
        root.material = MaterialComponent{};
        root.canvas = CanvasComponent{};
        child.id = "child";
        child.name = "Child";
        child.position = {1, 0, 0};
        child.parentId = root.id;
        SceneLayout source;
        source.objects = {root, child};
        const auto sourceJson = source.Serialize();
        const auto definition = WorldDefinition::Compile(source);
        WorldState world(definition), other(definition);
        const auto parent = *world.Read().Resolve(root.id), entity = *world.Read().Resolve(child.id);
        const auto adapter = MakeWorldReadView(world.Read());
        Require(adapter.ObjectIds().size() == 2 && adapter.Transform(child.id)->position == child.position &&
                    adapter.Component(root.id, &ScenePlacement::material)->roughness == .5f,
                "runtime-read-adapter-resolves-values-without-layout-export");
        Require(world.Read().Alive(entity) && !other.Read().Alive(entity), "runtime-handles-reject-other-world");
        Require(world.Read().With<MaterialComponent>().size() == 1 && world.Read().Entities().size() == 2,
                "runtime-component-tables-are-sparse");
        std::string error;
        Engine::CpuProfiler::Take();
        {
            auto transaction = world.BeginTransaction();
            Require(transaction.SetLocalTransform(entity, {{3, 0, 0}, {}, {1, 1, 1}}, error).succeeded,
                    "runtime-stage-transform");
            Require(world.Read().Transform(entity)->position == child.position &&
                        transaction.Read().Transform(entity)->position[0] == 3,
                    "runtime-candidate-is-not-public");
            Require(world.TakeChanges().Empty(), "runtime-does-not-publish-before-commit");
            auto material = *transaction.Read().Component<MaterialComponent>(parent);
            material.roughness = .25f;
            Require(transaction.SetComponent(parent, std::optional{material}, error).succeeded &&
                        world.Read().Component<MaterialComponent>(parent)->roughness == .5f,
                    "runtime-stage-component");
            auto canvas = *transaction.Read().Component<CanvasComponent>(parent);
            canvas.stateDefaults["counter"] = 1;
            canvas.menu = DefaultMenuConfiguration();
            Require(transaction.SetComponent(parent, std::optional{canvas}, error).succeeded &&
                        !world.Read().Component<CanvasComponent>(parent)->menu,
                    "runtime-stage-nested-map-and-optional");
            Require(transaction.Prepare(error), "runtime-prepare-values");
            bool committed;
            TestSupport::AllocationCounts allocations;
            {
                TestSupport::AllocationProbe probe(true);
                committed = transaction.Commit();
                allocations = probe.Counts();
            }
            Require(committed && allocations.count == 0, "runtime-commit-does-not-allocate");
            Require(!transaction.Commit() && !transaction.Read().Alive(entity), "runtime-transaction-is-once-only");
        }
        Require(world.Read().Transform(entity)->position[0] == 3 &&
                    world.Read().Component<MaterialComponent>(parent)->roughness == .25f,
                "runtime-publishes-values-together");
        Require(world.Read().Component<CanvasComponent>(parent)->menu &&
                    world.Read().Component<CanvasComponent>(parent)->stateDefaults.at("counter") == 1,
                "runtime-publishes-prepared-nested-values");
        Require(world.Read().WorldMatrix(entity)->_41 == 5, "runtime-resolves-parent-matrix");
        const auto notices = world.TakeChanges();
        Require(notices.transforms.size() == 1 && notices.components.size() == 2 && world.TakeChanges().Empty(),
                "runtime-publishes-change-set-once");
        Require(definition->Source().Serialize() == sourceJson && source.Serialize() == sourceJson,
                "runtime-does-not-mutate-authoring-definition");
        const auto samples = Engine::CpuProfiler::Take();
        Require(std::none_of(samples.begin(), samples.end(),
                             [](const auto& sample)
                             {
                                 return sample.name == "Runtime authoring export";
                             }),
                "runtime-value-transaction-does-not-export-layout");
        {
            auto transaction = world.BeginTransaction();
            transaction.SetLocalTransform(entity, {{8, 0, 0}, {}, {1, 1, 1}}, error);
            auto material = *transaction.Read().Component<MaterialComponent>(parent);
            material.roughness = .75f;
            transaction.SetComponent(parent, std::optional{material}, error);
        }
        Require(world.Read().Transform(entity)->position[0] == 3 &&
                    world.Read().Component<MaterialComponent>(parent)->roughness == .25f && world.TakeChanges().Empty(),
                "runtime-abort-discards-all-write-candidates");
        {
            auto transaction = world.BeginTransaction();
            transaction.SetLocalTransform(entity, {{9, 0, 0}, {}, {1, 1, 1}}, error);
            transaction.SetLocalTransform(entity, {{3, 0, 0}, {}, {1, 1, 1}}, error);
            Require(transaction.Prepare(error) && transaction.Commit(), "runtime-commit-reverted-candidate");
        }
        Require(world.TakeChanges().Empty(), "runtime-net-zero-value-does-not-notify");
        {
            auto transaction = world.BeginTransaction();
            transaction.SetLocalTransform(entity, {{9, 0, 0}, {}, {1, 1, 1}}, error);
            Require(!transaction.Commit(), "runtime-rejects-unprepared-commit");
        }
        Require(world.Read().Transform(entity)->position[0] == 3, "runtime-unprepared-commit-retains-state");
        {
            auto transaction = world.BeginTransaction();
            auto first = *transaction.Read().Transform(parent), second = *transaction.Read().Transform(entity);
            first.scale = second.scale = {1e10f, 1e10f, 1e10f};
            Require(transaction.SetLocalTransform(parent, first, error).succeeded &&
                        transaction.SetLocalTransform(entity, second, error).succeeded && !transaction.Prepare(error),
                    "runtime-rejects-inherited-overflow-before-publication");
        }
        Require(world.Read().Transform(parent)->scale == root.scale && world.TakeChanges().Empty(),
                "runtime-failed-prepare-retains-state");
        EntityHandle escaped;
        {
            auto transaction = world.BeginTransaction();
            ScenePlacement spawned;
            spawned.id = "spawned";
            spawned.name = "Spawned";
            Require(transaction.ApplyStructure(AddObjectEdit{spawned}, error).succeeded, "runtime-stage-spawn");
            escaped = *transaction.Read().Resolve(spawned.id);
            Require(!world.Read().Resolve(spawned.id), "runtime-spawn-is-not-public");
        }
        EntityHandle spawned;
        {
            auto transaction = world.BeginTransaction();
            ScenePlacement object;
            object.id = "spawned";
            object.name = "Spawned";
            Require(transaction.ApplyStructure(AddObjectEdit{object}, error).succeeded && transaction.Prepare(error) &&
                        transaction.Commit(),
                    "runtime-commit-spawn");
            spawned = *world.Read().Resolve(object.id);
        }
        Require(spawned != escaped && !world.Read().Alive(escaped) && world.Read().Alive(entity),
                "runtime-aborted-spawn-handle-never-aliases-retry");
        {
            auto transaction = world.BeginTransaction();
            Require(transaction.ApplyStructure(RemoveObjectsEdit{{"spawned"}}, error).succeeded &&
                        transaction.Prepare(error) && transaction.Commit(),
                    "runtime-commit-destroy");
        }
        Require(!world.Read().Alive(spawned), "runtime-destroy-invalidates-handle");
        {
            auto transaction = world.BeginTransaction();
            ScenePlacement object;
            object.id = "replacement";
            object.name = "Replacement";
            Require(transaction.ApplyStructure(AddObjectEdit{object}, error).succeeded && transaction.Prepare(error) &&
                        transaction.Commit(),
                    "runtime-reuse-slot");
            const auto replacement = *world.Read().Resolve(object.id);
            Require(replacement.slot == spawned.slot && replacement.generation != spawned.generation &&
                        !world.Read().Alive(spawned),
                    "runtime-slot-reuse-changes-generation");
        }
        const auto pose = *world.Read().WorldMatrix(entity);
        {
            auto transaction = world.BeginTransaction();
            Require(transaction.ApplyStructure(ParentEdit{{child.id}, {}}, error).succeeded &&
                        transaction.Prepare(error) && transaction.Commit(),
                    "runtime-parent-command");
        }
        Require(world.Read().WorldMatrix(entity)->_41 == pose._41 && !world.Read().Parent(entity),
                "runtime-parent-command-preserves-world-pose");
        Require(definition->Source().Serialize() == sourceJson, "runtime-structure-retains-old-definition-generation");
        world.TakeChanges();
        const auto previous = *world.Read().Resolve("replacement");
        {
            auto transaction = world.BeginTransaction();
            ScenePlacement replacement;
            replacement.id = "replacement";
            replacement.name = "Replacement";
            Require(transaction.ApplyStructure(RemoveObjectsEdit{{replacement.id}}, error).succeeded &&
                        transaction.ApplyStructure(AddObjectEdit{replacement}, error).succeeded &&
                        transaction.Prepare(error) && transaction.Commit(),
                    "runtime-recreate-identical-entity");
        }
        const auto lifetime = world.TakeChanges();
        Require(!world.Read().Alive(previous) && lifetime.structureChanged && lifetime.objects.size() == 2 &&
                    lifetime.objects[0].kind == SceneChangeKind::Removed &&
                    lifetime.objects[1].kind == SceneChangeKind::Added,
                "runtime-identical-data-still-publishes-new-lifetime");
    }

    void ValidatePages()
    {
        SceneLayout layout;
        for (size_t index = 0; index < 130; ++index)
        {
            ScenePlacement object;
            object.id = "paged-" + std::to_string(index);
            object.name = object.id;
            object.material = MaterialComponent{};
            layout.objects.push_back(std::move(object));
        }
        WorldState world(WorldDefinition::Compile(layout));
        std::string error;
        {
            auto transaction = world.BeginTransaction();
            for (const auto index : {size_t{0}, size_t{63}, size_t{64}, size_t{129}})
            {
                const auto entity = *transaction.Read().Resolve("paged-" + std::to_string(index));
                auto material = *transaction.Read().Component<MaterialComponent>(entity);
                material.roughness = .1f;
                Require(transaction.SetComponent(entity, std::optional{material}, error).succeeded,
                        "runtime-stage-page-boundary-value");
            }
            Require(transaction.Prepare(error), "runtime-prepare-multiple-pages");
            bool committed;
            TestSupport::AllocationCounts memory;
            {
                TestSupport::AllocationProbe probe(true);
                committed = transaction.Commit();
                memory = probe.Counts();
            }
            Require(committed && memory.count == 0, "runtime-page-commit-does-not-allocate");
        }
        for (const auto index : {size_t{0}, size_t{63}, size_t{64}, size_t{129}})
            Require(world.Read()
                            .Component<MaterialComponent>(*world.Read().Resolve("paged-" + std::to_string(index)))
                            ->roughness == .1f,
                    "runtime-page-boundary-value-is-published");
        Require(world.Read().Component<MaterialComponent>(*world.Read().Resolve("paged-20"))->roughness == .5f,
                "runtime-unchanged-value-in-touched-page-is-retained");
    }

    void ValidateComponentLifetimes()
    {
        ScenePlacement object;
        object.id = "owner";
        object.material = MaterialComponent{};
        ScriptComponent first, second;
        first.id = "first";
        second.id = "second";
        object.scripts = {first, second};
        SceneLayout layout;
        layout.objects.push_back(object);
        const auto definition = WorldDefinition::Compile(layout);
        WorldState world(definition), other(definition);
        const auto entity = *world.Read().Resolve(object.id);
        const auto material = *world.Read().Instance<MaterialComponent>(entity);
        const auto retained = world.Read().ComponentLease<MaterialComponent>(entity);
        const auto script = *world.Read().RepeatedInstance<ScriptComponent>(entity, first.id);
        Require(world.Read().Alive(material) && !other.Read().Alive(material), "component-token-rejects-other-world");
        std::string error;
        {
            auto transaction = world.BeginTransaction();
            Require(transaction.SetComponent<MaterialComponent>(entity, {}, error).succeeded &&
                        transaction.SetComponent(entity, object.material, error).succeeded,
                    "component-remove-and-recreate");
            Require(!transaction.Read().Alive(material) && world.Read().Alive(material),
                    "component-recreation-is-only-in-candidate");
            Require(transaction.SetRepeated(entity, std::vector{second, first}, error).succeeded &&
                        transaction.Read().RepeatedInstance<ScriptComponent>(entity, first.id) == script,
                    "component-reorder-preserves-instance");
            Require(transaction.Prepare(error) && transaction.Commit(), "component-recreation-commit");
        }
        const auto notice = world.TakeChanges();
        Require(!world.Read().Alive(material) && notice.structureChanged && notice.components.size() >= 2,
                "component-identical-data-publishes-new-lifetime");
        {
            auto transaction = world.BeginTransaction();
            auto changed = *world.Read().Component<MaterialComponent>(entity);
            changed.roughness = .17f;
            transaction.SetComponent(entity, std::optional{changed}, error);
            const auto candidate = transaction.Read().ComponentLease<MaterialComponent>(entity);
            Require(candidate->roughness == .17f && retained->roughness == .5f,
                    "component-lease-holds-independent-candidate-and-old-generation");
            Require(transaction.Prepare(error) && transaction.Commit() && candidate->roughness == .17f &&
                        retained->roughness == .5f,
                    "component-lease-remains-valid-after-page-publication");
        }
        std::shared_ptr<const MaterialComponent> expiredOwner;
        {
            WorldState temporary(definition);
            expiredOwner = temporary.Read().ComponentLease<MaterialComponent>(*temporary.Read().Resolve(object.id));
        }
        Require(expiredOwner && expiredOwner->roughness == .5f,
                "component-lease-retains-values-after-world-destruction");
        ComponentInstanceHandle aborted;
        {
            auto transaction = world.BeginTransaction();
            transaction.SetComponent<MaterialComponent>(entity, {}, error);
            transaction.SetComponent(entity, object.material, error);
            aborted = *transaction.Read().Instance<MaterialComponent>(entity);
        }
        {
            auto transaction = world.BeginTransaction();
            transaction.SetComponent<MaterialComponent>(entity, {}, error);
            transaction.SetComponent(entity, object.material, error);
            Require(transaction.Read().Instance<MaterialComponent>(entity) != aborted,
                    "component-aborted-generation-is-never-reused");
        }
    }
    void ValidateVelocityWrites()
    {
        SceneLayout layout;
        ScenePlacement object;
        object.id = "body";
        object.rigidBody.emplace();
        object.boxCollider.emplace();
        layout.objects.push_back(object);
        WorldState world(WorldDefinition::Compile(layout));
        const auto entity = *world.Read().Resolve("body");
        auto tx = world.BeginTransaction();
        std::string error;
        auto body = *tx.Read().ComponentLease<RigidBodyComponent>(entity);
        body.velocity = {1, 2, 3};
        body.angularVelocity = {4, 5, 6};
        Require(tx.SetComponent(entity, std::optional{body}, error).succeeded && tx.Prepare(error),
                "runtime-velocity-only-write-prepares-without-authoring-json");
        auto invalid = body;
        invalid.velocity[0] = std::numeric_limits<float>::quiet_NaN();
        Require(!tx.SetComponent(entity, std::optional{invalid}, error).succeeded &&
                    tx.Read().ComponentLease<RigidBodyComponent>(entity)->velocity == body.velocity,
                "runtime-velocity-fast-path-rejects-nonfinite-without-changing-candidate");
        invalid = body;
        invalid.angularVelocity[0] = 100001;
        Require(!tx.SetComponent(entity, std::optional{invalid}, error).succeeded,
                "runtime-velocity-fast-path-keeps-angular-limit");
        invalid = body;
        invalid.mass = 0;
        Require(!tx.SetComponent(entity, std::optional{invalid}, error).succeeded,
                "runtime-velocity-setting-change-still-validates-mass");
        invalid = body;
        invalid.friction = 2;
        Require(!tx.SetComponent(entity, std::optional{invalid}, error).succeeded,
                "runtime-velocity-setting-change-still-validates-friction");
        Require(world.Read().ComponentLease<RigidBodyComponent>(entity)->velocity == object.rigidBody->velocity &&
                    tx.Prepare(error) && tx.Commit() &&
                    world.Read().ComponentLease<RigidBodyComponent>(entity)->velocity == body.velocity,
                "runtime-velocity-write-retains-success-boundary-after-rejected-writes");
    }
    void ValidatePlacementPublication()
    {
        ScenePlacement target;
#define ENGINE_TEST_OPTIONAL(Type, member, references, assets, order) target.member.emplace();
#define ENGINE_TEST_REPEATED(Type, member, references, assets, order) target.member.emplace_back();
        ENGINE_SCENE_COMPONENTS(ENGINE_TEST_OPTIONAL, ENGINE_TEST_REPEATED)
#undef ENGINE_TEST_OPTIONAL
#undef ENGINE_TEST_REPEATED
        target.prefab.emplace();
        target.canvas->menu.emplace();
        target.canvas->stateDefaults["visible"] = 1;
        auto prepared = target;
        prepared.name = "prepared";
        TestSupport::AllocationCounts counts;
        {
            TestSupport::AllocationProbe probe(true);
            target = std::move(prepared);
            counts = probe.Counts();
        }
        Require(target.name == "prepared" && target.canvas->stateDefaults.at("visible") == 1 && counts.count == 0,
                "runtime-same-shape-placement-move-assignment-does-not-allocate");
    }
    unsigned int Recomputed()
    {
        unsigned int result = 0;
        for (const auto& sample : Engine::CpuProfiler::Take())
            if (sample.name == "Runtime transform recompute")
                result += sample.calls;
        return result;
    }
    void ValidateDirtyTransforms()
    {
        SceneLayout layout;
        ScenePlacement root, child, leaf;
        root.id = "root";
        root.position[0] = 1;
        child.id = "child";
        child.parentId = "root";
        child.position[0] = 2;
        leaf.id = "leaf";
        leaf.parentId = "child";
        leaf.position[0] = 3;
        layout.objects = {leaf, child, root};
        for (unsigned int index = 0; index < 256; ++index)
        {
            ScenePlacement separate;
            separate.id = "separate-" + std::to_string(index);
            layout.objects.push_back(separate);
        }
        WorldState world(WorldDefinition::Compile(layout));
        const auto leafEntity = *world.Read().Resolve("leaf"), rootEntity = *world.Read().Resolve("root");
        std::string error;
        {
            auto tx = world.BeginTransaction();
            TestSupport::AllocationCounts counts;
            bool read = false;
            {
                TestSupport::AllocationProbe probe(true);
                read = tx.Read().WorldMatrix(leafEntity)->_41 == 6;
                counts = probe.Counts();
            }
            Require(read && counts.count == 0,
                    "runtime-readonly-candidate-reuses-published-matrices-without-allocation");
            Engine::CpuProfiler::Take();
            tx.SetLocalTransform(leafEntity, {{4, 0, 0}, {}, {1, 1, 1}}, error);
            Require(tx.Read().WorldMatrix(leafEntity)->_41 == 7 && Recomputed() == 1,
                    "runtime-leaf-edit-recomputes-only-leaf");
            Require(tx.Read().WorldMatrix(leafEntity)->_41 == 7 && Recomputed() == 0,
                    "runtime-repeat-read-reuses-candidate-matrix");
            tx.SetLocalTransform(rootEntity, {{10, 0, 0}, {}, {1, 1, 1}}, error);
            Require(tx.Read().WorldMatrix(leafEntity)->_41 == 16 && Recomputed() == 3,
                    "runtime-parent-edit-invalidates-descendants-after-prior-read");
            tx.SetLocalTransform(leafEntity, {{5, 0, 0}, {}, {1, 1, 1}}, error);
            Require(tx.Read().WorldMatrix(leafEntity)->_41 == 17 && Recomputed() == 1,
                    "runtime-second-leaf-edit-keeps-clean-parent-and-unrelated-branches");
            Require(tx.Prepare(error) && Recomputed() == 0, "runtime-prepare-keeps-already-validated-dirty-matrices");
            bool committed = false;
            {
                TestSupport::AllocationProbe probe(true);
                committed = tx.Commit();
                counts = probe.Counts();
            }
            Require(committed && counts.count == 0 && world.Read().WorldMatrix(leafEntity)->_41 == 17,
                    "runtime-dirty-matrix-publication-does-not-allocate");
        }
        {
            auto tx = world.BeginTransaction();
            tx.SetLocalTransform(rootEntity, {{20, 0, 0}, {}, {1, 1, 1}}, error);
            Require(tx.Read().WorldMatrix(leafEntity)->_41 == 27, "runtime-dirty-rollback-candidate-resolves");
        }
        Require(world.Read().WorldMatrix(leafEntity)->_41 == 17, "runtime-dirty-rollback-retains-published-matrices");
        {
            auto tx = world.BeginTransaction();
            tx.SetLocalTransform(leafEntity, {{6, 0, 0}, {}, {1, 1, 1}}, error);
            Require(tx.Read().WorldMatrix(leafEntity)->_41 == 18 && tx.Prepare(error) && tx.Commit(),
                    "runtime-reused-scratch-starts-from-published-matrices");
        }
        {
            auto tx = world.BeginTransaction();
            Require(tx.ApplyStructure(RemoveObjectsEdit{{"child"}}, error).succeeded,
                    "runtime-dirty-hierarchy-removal-prepares");
            Require(!tx.Read().Parent(leafEntity), "runtime-dirty-hierarchy-removal-detaches-surviving-child");
            tx.Read().WorldMatrix(leafEntity);
            Engine::CpuProfiler::Take();
            tx.SetLocalTransform(rootEntity, {{30, 0, 0}, {}, {1, 1, 1}}, error);
            Require(tx.Prepare(error) && Recomputed() == 1 && tx.Commit() &&
                        world.Read().WorldMatrix(leafEntity)->_41 == 18,
                    "runtime-dirty-propagation-uses-new-hierarchy-after-structural-edit");
        }
    }

}

int main()
{
    std::set_terminate(
        []()
        {
            std::fputs("FAIL: unexpected terminate during runtime validation\n", stderr);
            std::_Exit(86);
        });
    std::fputs("Runtime validation started\n", stderr);
    try
    {
        Validate();
        ValidatePages();
        ValidateComponentLifetimes();
        ValidateDirtyTransforms();
        ValidatePlacementPublication();
        ValidateVelocityWrites();
        std::cout << "World state contracts: " << contracts << ", failures: 0\n";
        return 0;
    }
    catch (const std::exception& failure)
    {
        std::cerr << "FAIL: " << failure.what() << '\n';
        return 1;
    }
}
