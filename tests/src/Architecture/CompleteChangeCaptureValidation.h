#pragma once
#include <SceneRuntime/Scene/SceneWorld.h>

namespace CompleteChangeCaptureValidation
{
    /// <summary>構造・全体設定・コンポーネントID・資源依存と、描画なしの編集を検証します。</summary>
    template <class Contract> void Data(const std::filesystem::path& root, Contract contract)
    {
        using namespace SceneRuntime;
        ScenePlacement object;
        object.id = "complete-owner";
        object.material.emplace().asset = "Assets/Materials/Old.mat";
        SceneLayout before;
        before.objects = {object};
        auto after = before;
        after.objects[0].material->asset = "Assets/Materials/New.mat";
        auto changes = SceneChangeSet::Compare(before, after);
        contract("capture-resource-dependency", changes.components.size() == 1 && changes.resources.size() == 1 &&
                                                    changes.components[0].componentId == "material" &&
                                                    changes.resources[0].before[0] == object.material->asset &&
                                                    changes.resources[0].after[0] == after.objects[0].material->asset &&
                                                    !changes.structureChanged);
        after = before;
        after.objects[0].material->roughness = .75f;
        changes = SceneChangeSet::Compare(before, after);
        contract("capture-value-without-dependency-change",
                 changes.components.size() == 1 && changes.resources.empty());
        after.objects[0].material->id = "replacement";
        changes = SceneChangeSet::Compare(before, after);
        contract("capture-component-instance-replacement", changes.structureChanged && changes.components.size() == 2 &&
                                                               changes.components[0].kind == SceneChangeKind::Removed &&
                                                               changes.components[1].kind == SceneChangeKind::Added);
        after = before;
        after.objects.clear();
        changes = SceneChangeSet::Compare(before, after);
        contract("capture-object-remove", changes.objects.size() == 1 && changes.structureChanged &&
                                              changes.objects[0].kind == SceneChangeKind::Removed &&
                                              changes.resources.size() == 1);
        changes = SceneChangeSet::Compare(after, before);
        contract("capture-object-add",
                 changes.objects.size() == 1 && changes.objects[0].kind == SceneChangeKind::Added);
        after = before;
        after.objects[0].name = "renamed";
        after.objects[0].persistent = true;
        changes = SceneChangeSet::Compare(before, after);
        contract("capture-object-fields",
                 changes.fields.size() == 2 && !changes.structureChanged && changes.resources.empty());
        after = before;
        after.objects[0].scripts = {{"first", true, "Bob", {}}, {"second", true, "Spin", {}}};
        auto reordered = after;
        std::reverse(reordered.objects[0].scripts.begin(), reordered.objects[0].scripts.end());
        changes = SceneChangeSet::Compare(after, reordered);
        contract("capture-script-order",
                 changes.structureChanged && changes.components.empty() && changes.fields.size() == 1);
        reordered.objects[0].scripts[0].parameters["speed"] = 20;
        changes = SceneChangeSet::Compare(after, reordered);
        contract("capture-script-stable-instance",
                 changes.components.size() == 1 && changes.components[0].componentId == "second");
        after = before;
        after.settings.skyShader = "Engine/Shaders/Sky.hlsl";
        after.assetReferences["Assets/Materials/Old.mat"] = std::string(32, 'a');
        after.prefabSlots["external"] = "complete-owner";
        changes = SceneChangeSet::Compare(before, after);
        contract("capture-scene-fields",
                 changes.fields.size() == 3 && changes.resources.size() == 2 && changes.structureChanged);
        auto merge = changes;
        merge.Merge(changes);
        contract("capture-complete-merge-idempotent",
                 merge.fields.size() == changes.fields.size() && merge.resources.size() == changes.resources.size());
        SceneChangeSet swapped;
        swapped.Swap(merge);
        contract("capture-complete-swap", merge.Empty() && !swapped.Empty());
        SceneChangeSet invalidChanges;
        bool rejectedKind = false, rejectedId = false;
        try
        {
            invalidChanges.RecordObject(object, SceneChangeKind::Modified);
        }
        catch (const std::invalid_argument&)
        {
            rejectedKind = true;
        }
        auto otherId = object;
        otherId.id = "different";
        try
        {
            invalidChanges.Record(object, otherId);
        }
        catch (const std::invalid_argument&)
        {
            rejectedId = true;
        }
        contract("capture-invalid-lifetime-kind", rejectedKind && invalidChanges.Empty());
        contract("capture-invalid-transform-id", rejectedId && invalidChanges.Empty());
        SceneLayout many;
        SceneChangeSet states;
        for (size_t index = 0; index < 5000; ++index)
        {
            ScenePlacement item;
            item.id = "many-" + std::to_string(index);
            states.fields.push_back({item.id, "script", "runtime.state"});
            many.objects.push_back(std::move(item));
        }
        auto moved = many;
        for (auto& item : moved.objects)
            item.position[0] += 1;
        const auto manyChanges = SceneChangeSet::Compare(many, moved);
        contract("capture-5000-transforms", manyChanges.transforms.size() == 5000 && manyChanges.objects.empty());
        SceneChangeSet manyStates;
        manyStates.Merge(states);
        manyStates.Merge(states);
        contract("capture-5000-merged-states", manyStates.fields.size() == 5000);
        PhysicsWorld impulseQueue;
        bool rejectedImpulses = true;
        for (size_t index = 0; index < 4096; ++index)
            rejectedImpulses &= !impulseQueue.AddImpulse("rejected-" + std::to_string(index), {1000001, 0, 0});
        contract("capture-rejected-impulses-retain-capacity",
                 rejectedImpulses && impulseQueue.AddImpulse("valid", {1, 0, 0}));

        SceneLayout layout;
        ScenePlacement parent, child;
        parent.id = "parent";
        parent.position[0] = 2;
        child.id = "child";
        child.parentId = parent.id;
        child.position[0] = 3;
        layout.objects = {parent, child};
        auto document = std::make_shared<SceneDocumentData>(layout);
        SceneWorld world(document);
        std::string error;
        contract("capture-complete-headless", world.ReplaceLayout(layout, root, error));
        world.TakeChanges();
        auto value = *document->Read().Object(parent.id);
        value.pointLight.emplace().intensity = 2;
        auto edited = world.ApplyComponents(parent.id, value, root, error);
        contract("capture-typed-components", edited.succeeded && edited.changes.components.size() == 1 &&
                                                 world.TakeChanges().components.size() == 1);
        auto light = document->Read().Component(parent.id, &ScenePlacement::pointLight);
        light->intensity = 9;
        contract("capture-component-read-is-value",
                 world.Read().Component(parent.id, &ScenePlacement::pointLight)->intensity == 2 &&
                     document->Read().ObjectIds().size() == 2 && document->Read().Settings() == layout.settings);
        const auto version = world.RenderVersion();
        contract("capture-components-noop", world.ApplyComponents(parent.id, value, root, error).changes.Empty() &&
                                                world.RenderVersion() == version && world.TakeChanges().Empty());
        contract("capture-name-change", world.RenameObject(parent.id, "Renamed") &&
                                            world.TakeChanges().fields.size() == 1 &&
                                            world.RenameObject(parent.id, "Renamed") && world.TakeChanges().Empty());
        auto settings = world.Layout().settings;
        settings.gravity[1] = -12;
        contract("capture-settings-change", world.SetSettings(settings, error) &&
                                                world.TakeChanges().fields.size() == 1 &&
                                                world.SetSettings(settings, error) && world.TakeChanges().Empty());
        std::string added, duplicate;
        ScenePlacement empty;
        contract("capture-public-add",
                 world.AddObject(empty, root, added, error) && world.TakeChanges().objects.size() == 1);
        contract("capture-public-duplicate",
                 world.DuplicateObject(added, {}, duplicate, error) && world.TakeChanges().objects.size() == 1);
        contract("capture-public-remove", world.RemoveObjects({parent.id, added, duplicate}, error));
        changes = world.TakeChanges();
        contract("capture-remove-reparent", changes.objects.size() == 3 && changes.transforms.size() == 1 &&
                                                changes.transforms[0].Has(TransformField::Parent) &&
                                                world.Read().Transform(child.id)->position[0] == 5);
        const auto unchanged = world.Layout().Serialize();
        contract("capture-structural-failure", !world.RemoveObjects({"missing"}, error) &&
                                                   world.Layout().Serialize() == unchanged &&
                                                   world.TakeChanges().Empty());

        SceneLayout staged;
        parent.position[0] = 4;
        child.parentId.clear();
        child.position[0] = 1;
        staged.objects = {parent, child};
        size_t counter = 1;
        ScriptScene scene(staged, counter);
        std::map<std::string, float> parameters, state;
        ScriptContext context(staged.objects[1], parameters, state, .016, nullptr, nullptr, &scene);
        PointLightComponent point;
        BoxColliderComponent collider;
        const auto one = context.SetComponent(&ScenePlacement::pointLight, std::optional{point});
        const auto two = context.SetComponent(&ScenePlacement::boxCollider, std::optional{collider});
        context.SetParent(parent.id);
        contract("capture-staged-component-visibility",
                 one.succeeded && two.succeeded && !scene.Read().Component(child.id, &ScenePlacement::pointLight));
        scene.Commit();
        contract("capture-staged-component-composition",
                 staged.objects[1].pointLight && staged.objects[1].boxCollider &&
                     staged.objects[1].parentId == parent.id && staged.objects[1].position[0] == -3);
    }

    /// <summary>生成・親変更・Script状態・Pause・終了準備の成功境界を実行ホストで検証します。</summary>
    template <class Contract>
    void Runtime(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& root, Contract contract)
    {
        using namespace SceneRuntime;
        SceneWorld world;
        int mode = 0, stops = 0;
        bool failResources = false, failPause = false, earlyNotice = false, reentered = false;
        ScriptDefinition definition;
        definition.start = [&](ScriptContext& context)
        {
            context.state["ticks"] = 0;
            earlyNotice |= !world.TakeChanges().Empty();
            reentered |= world.ApplyLocalTransform(context.object.id, {}).succeeded;
        };
        definition.update = [&](ScriptContext& context)
        {
            ++context.state["ticks"];
            (*context.dataState)["tick"] = {context.state["ticks"]};
            if (mode == 1)
            {
                PointLightComponent light;
                context.SetComponent(&ScenePlacement::pointLight, std::optional{light});
                context.SetParent("parent");
                context.scene->Spawn(ScenePlacement{});
            }
            if (mode == 2)
                context.SetParent(context.object.id);
            if (mode == 3)
            {
                auto candidate = context.object;
                candidate.scripts.clear();
                context.SetComponents(candidate);
            }
        };
        definition.pause = [&](ScriptContext& context, bool paused)
        {
            if (mode != 4)
                context.object.position[1] = paused ? 7.0f : 0.0f;
            context.state["paused"] = paused ? 1.0f : 0.0f;
            earlyNotice |= !world.TakeChanges().Empty();
            if (failPause)
                throw std::runtime_error("injected Pause failure");
        };
        definition.prepareStop = [](ScriptContext& context)
        {
            context.object.name = "stopped";
        };
        definition.stop = [&](const ScriptStopContext&)
        {
            ++stops;
        };
        contract("capture-complete-register", ScriptRegistry::Register("CompleteCapture", definition));
        SceneLayout layout;
        ScenePlacement parent, actor;
        parent.id = "parent";
        parent.position[0] = 4;
        actor.id = "actor";
        actor.position[0] = 1;
        actor.scripts.push_back({"complete", true, "CompleteCapture", {}});
        layout.objects = {parent, actor};
        std::string error;
        contract("capture-complete-runtime-setup",
                 world.Initialize(renderer, root, layout, root / "Engine/Shaders/Mesh.hlsl", &error));
        world.TakeChanges();
        contract("capture-start-boundary",
                 world.StartScripts(error) && !earlyNotice && !reentered && !world.TakeChanges().fields.empty());
        world.SetRuntimePreparation(
            [&](const SceneLayout&, std::string&)
            {
                return !failResources;
            });
        contract("capture-runtime-state-only", world.UpdateComponents(.016));
        auto changes = world.TakeChanges();
        contract("capture-runtime-state-fields",
                 changes.transforms.empty() && changes.fields.size() == 2 && changes.objects.empty());
        const auto unchanged = world.Layout().Serialize();
        mode = 1;
        failResources = true;
        contract("capture-structure-resource-rollback", !world.UpdateComponents(.016) &&
                                                            world.Layout().Serialize() == unchanged &&
                                                            world.TakeChanges().Empty());
        failResources = false;
        contract("capture-structure-frame-commit", world.UpdateComponents(.016));
        changes = world.TakeChanges();
        contract("capture-structure-frame-fields", changes.structureChanged && changes.objects.size() == 1 &&
                                                       changes.components.size() == 1 &&
                                                       world.Read().Object("actor")->parentId == "parent" &&
                                                       world.Read().Transform("actor")->position[0] == -3);
        mode = 2;
        const auto beforeCycle = world.Layout().Serialize();
        contract("capture-parent-cycle-rollback", !world.UpdateComponents(.016) &&
                                                      world.Layout().Serialize() == beforeCycle &&
                                                      world.TakeChanges().Empty());
        mode = 0;
        failPause = true;
        contract("capture-pause-callback-rollback", !world.SetScriptsPaused(true, error) &&
                                                        world.Layout().Serialize() == beforeCycle &&
                                                        world.TakeChanges().Empty());
        failPause = false;
        failResources = true;
        contract("capture-pause-resource-rollback", !world.SetScriptsPaused(true, error) &&
                                                        world.Layout().Serialize() == beforeCycle &&
                                                        world.TakeChanges().Empty());
        failResources = false;
        contract("capture-pause-commit", world.SetScriptsPaused(true, error));
        changes = world.TakeChanges();
        contract("capture-pause-fields", changes.transforms.size() == 1 && changes.fields.size() == 2 && !earlyNotice);
        contract("capture-pause-noop", world.SetScriptsPaused(true, error) && world.TakeChanges().Empty());
        contract("capture-resume-commit", world.SetScriptsPaused(false, error));
        world.TakeChanges();
        mode = 4;
        failResources = true;
        contract("capture-state-only-pause-skips-resources", world.SetScriptsPaused(true, error));
        changes = world.TakeChanges();
        contract("capture-state-only-pause-fields", changes.transforms.empty() && changes.fields.size() == 2);
        failResources = false;
        contract("capture-state-only-resume", world.SetScriptsPaused(false, error));
        world.TakeChanges();
        mode = 3;
        const auto beforeStops = stops;
        contract("capture-stop-removal-frame", world.UpdateComponents(.016));
        contract("capture-stop-cleanup-frame", world.UpdateComponents(.016));
        contract("capture-stop-writeback", stops == beforeStops + 1 && world.Read().Object("actor")->name == "stopped");
        changes = world.TakeChanges();
        contract("capture-stop-fields",
                 changes.structureChanged && !changes.components.empty() && !changes.fields.empty());
        contract("capture-reload-success",
                 world.ReloadAssets(renderer, root, root / "Engine/Shaders/Mesh.hlsl", error));
        changes = world.TakeChanges();
        contract("capture-reload-notice", std::any_of(changes.fields.begin(), changes.fields.end(),
                                                      [](const auto& field)
                                                      {
                                                          return field.field == "resources.reload";
                                                      }));
        contract("capture-replacement-reset", world.ReplaceLayout(layout, root, error) && world.StartScripts(error));
        changes = world.TakeChanges();
        contract("capture-replacement-reset-notice", std::any_of(changes.fields.begin(), changes.fields.end(),
                                                                 [](const auto& field)
                                                                 {
                                                                     return field.field == "runtime.reset";
                                                                 }));
        auto replacement = world.Layout();
        replacement.objects[1].scripts.clear();
        const auto beforeReplacement = world.Layout().Serialize();
        const auto replacementStops = stops;
        failResources = true;
        contract("capture-replacement-stop-rollback", !world.ReplaceLayout(replacement, root, error, true) &&
                                                          world.Layout().Serialize() == beforeReplacement &&
                                                          stops == replacementStops && world.TakeChanges().Empty());
        failResources = false;
        contract("capture-replacement-stop-commit", world.ReplaceLayout(replacement, root, error, true) &&
                                                        stops == replacementStops + 1 &&
                                                        world.Read().Object("actor")->name == "stopped");
        contract("capture-replacement-stop-notice", !world.TakeChanges().Empty());
        contract("capture-direct-removal-reset", world.ReplaceLayout(layout, root, error) && world.StartScripts(error));
        world.TakeChanges();
        const auto removalStops = stops;
        contract("capture-direct-removal-stop", world.RemoveObjects({"actor"}, error) && stops == removalStops + 1);
        changes = world.TakeChanges();
        contract("capture-direct-removal-notice", changes.objects.size() == 1 && !changes.fields.empty());
        ScriptDefinition impulseDefinition;
        impulseDefinition.update = [](ScriptContext& context)
        {
            context.scene->AddImpulse(context.object.id, {1, 0, 0});
        };
        contract("capture-physics-register", ScriptRegistry::Register("CompleteImpulse", impulseDefinition));
        auto physicsLayout = layout;
        physicsLayout.settings.gravity = {0, 0, 0};
        physicsLayout.objects[1].rigidBody.emplace();
        physicsLayout.objects[1].boxCollider.emplace();
        physicsLayout.objects[1].scripts[0].behaviour = "CompleteImpulse";
        SceneWorld physical;
        contract("capture-physics-setup",
                 physical.Initialize(renderer, root, physicsLayout, root / "Engine/Shaders/Mesh.hlsl", &error) &&
                     physical.StartScripts(error));
        physical.TakeChanges();
        contract("capture-physics-tick", physical.UpdateFrame(1.0 / 60, 0, 0, false, true));
        changes = physical.TakeChanges();
        const auto hasPhysics = [](const SceneChangeSet& value)
        {
            return std::any_of(value.fields.begin(), value.fields.end(),
                               [](const auto& field)
                               {
                                   return field.field == "runtime.physics";
                               });
        };
        contract("capture-physics-tick-notice", hasPhysics(changes));
        contract("capture-physics-body-created", physical.PhysicsRaycast({1, 5, 0}, {0, -1, 0}, 10).has_value());
        contract("capture-physics-state-only-update", physical.UpdateComponents(.016));
        changes = physical.TakeChanges();
        contract("capture-physics-state-without-pose", hasPhysics(changes) && changes.transforms.empty() &&
                                                           changes.components.empty() && changes.objects.empty());
    }
}
