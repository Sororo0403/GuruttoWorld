#pragma once
#include <SceneRuntime/Scene/SceneWorld.h>
#include "../../../Editor/src/Editing/EditState.h"
#include "../../../Editor/src/Editing/EditHistory.h"
#include <limits>
#include <type_traits>
#include "CompleteChangeCaptureValidation.h"

namespace ChangeCaptureValidation
{
    /// <summary>読み取り値・同一値・失敗・親子編集とUndoをGPUなしで検証します。</summary>
    template <class Contract> void Run(const std::filesystem::path& root, Contract contract)
    {
        using namespace SceneRuntime;
        std::filesystem::create_directories(root);
        static_assert(!std::is_constructible_v<WorldReadView, SceneLayout&&>);
        static_assert(!std::is_constructible_v<WorldReadView, const SceneLayout&&>);
        ScenePlacement parent, child;
        parent.id = "parent";
        parent.position[0] = 1;
        child.id = "child";
        child.parentId = parent.id;
        child.position[0] = 2;
        SceneLayout layout;
        layout.objects = {parent, child};
        const auto snapshot = WorldReadView(layout).Transform(parent.id);
        contract("capture-read-values",
                 snapshot && snapshot->position[0] == 1 && !WorldReadView(layout).Transform("missing"));

        auto transform = *snapshot;
        transform.position[0] = 3;
        auto edited = parent;
        const auto result = SceneTransformEdit::Apply(edited, transform);
        contract("capture-typed-field", result.succeeded && result.changes.transforms.size() == 1 &&
                                            result.changes.transforms[0].objectId == parent.id &&
                                            result.changes.transforms[0].fields == TransformField::Position &&
                                            snapshot->position[0] == 1);
        contract("capture-same-value", SceneTransformEdit::Apply(edited, transform).changes.Empty());
        auto invalid = transform;
        invalid.scale[0] = 0;
        const auto rejected = SceneTransformEdit::Apply(edited, invalid);
        contract("capture-invalid-value", !rejected.succeeded && rejected.changes.Empty() && edited.scale[0] == 1);
        auto reordered = layout;
        std::reverse(reordered.objects.begin(), reordered.objects.end());
        contract("capture-stable-id", SceneChangeSet::CompareTransforms(layout, reordered).Empty());
        reordered.objects.back().position[0] += 1;
        reordered.objects.back().rotation[1] += .5f;
        const auto differences = SceneChangeSet::CompareTransforms(layout, reordered);
        contract("capture-field-union", differences.transforms.size() == 1 &&
                                            differences.transforms[0].Has(TransformField::Position) &&
                                            differences.transforms[0].Has(TransformField::Rotation) &&
                                            !differences.transforms[0].Has(TransformField::Scale));

        auto document = std::make_shared<SceneDocumentData>(layout);
        SceneWorld world(document);
        std::string error;
        contract("capture-headless-setup", world.ReplaceLayout(layout, root, error));
        world.TakeChanges();
        Editor::EditState edit;
        contract("capture-editor-path",
                 edit.SetLocalTransform(world, parent.id, transform.position, transform.rotation, transform.scale) &&
                     edit.HasChanges() && document->Layout().objects[0].position[0] == 3);
        auto published = world.TakeChanges();
        contract("capture-publish-once", published.transforms.size() == 1 &&
                                             published.transforms[0].fields == TransformField::Position &&
                                             world.TakeChanges().Empty());
        const auto version = world.RenderVersion();
        edit.MarkSaved();
        contract("capture-noop-preserves-projection",
                 edit.SetLocalTransform(world, parent.id, transform.position, transform.rotation, transform.scale) &&
                     !edit.HasChanges() && world.RenderVersion() == version && world.TakeChanges().Empty());
        transform.rotation[1] = .25f;
        contract("capture-pending-edit", world.ApplyLocalTransform(parent.id, transform).succeeded);
        const auto beforeFailure = world.Layout().Serialize();
        invalid = transform;
        invalid.position[0] = std::numeric_limits<float>::infinity();
        contract("capture-failure-retains-state", !world.ApplyLocalTransform(parent.id, invalid).succeeded &&
                                                      !world.ApplyLocalTransform("missing", transform).succeeded &&
                                                      world.Layout().Serialize() == beforeFailure);
        published = world.TakeChanges();
        contract("capture-failure-retains-notices",
                 published.transforms.size() == 1 && published.transforms[0].fields == TransformField::Rotation);

        transform.rotation = {};
        contract("capture-reset-rotation", world.ApplyLocalTransform(parent.id, transform).succeeded);
        world.TakeChanges();
        DirectX::XMFLOAT4X4 matrix;
        contract("capture-child-propagation", world.WorldMatrix(child.id, matrix) && matrix._41 == 5);
        contract("capture-parent-edit", world.SetParent(child.id, "", error));
        published = world.TakeChanges();
        contract("capture-parent-fields", published.transforms.size() == 1 &&
                                              published.transforms[0].objectId == child.id &&
                                              published.transforms[0].Has(TransformField::Parent) &&
                                              published.transforms[0].Has(TransformField::Position) &&
                                              world.WorldMatrix(child.id, matrix) && matrix._41 == 5);
        contract("capture-parent-failure", !world.SetParent(child.id, child.id, error) && world.TakeChanges().Empty());

        Editor::EditHistory history;
        const auto beforeUndo = world.Layout().Serialize();
        history.Reset({beforeUndo, parent.id, {parent.id}});
        transform.scale[0] = -2;
        contract("capture-mirrored-transform", world.ApplyLocalTransform(parent.id, transform).succeeded);
        const auto afterUndo = world.Layout().Serialize();
        history.Observe({afterUndo, parent.id, {parent.id}}, "transform");
        history.Commit();
        world.TakeChanges();
        contract("capture-undo", history.CanUndo() &&
                                     world.ReplaceLayout(SceneLayout::Parse(history.Target(false).json), root, error) &&
                                     world.Layout().Serialize() == beforeUndo);
        history.Applied(false);
        published = world.TakeChanges();
        contract("capture-undo-notice",
                 published.transforms.size() == 1 && published.transforms[0].fields == TransformField::Scale);
        contract("capture-redo", history.CanRedo() &&
                                     world.ReplaceLayout(SceneLayout::Parse(history.Target(true).json), root, error) &&
                                     world.Layout().Serialize() == afterUndo);
        published = world.TakeChanges();
        contract("capture-redo-notice",
                 published.transforms.size() == 1 && published.transforms[0].fields == TransformField::Scale);

        auto merged = result.changes;
        merged.Merge(differences);
        merged.Merge(merged);
        contract("capture-merge-by-id", merged.transforms.size() == 1 &&
                                            merged.transforms[0].Has(TransformField::Position) &&
                                            merged.transforms[0].Has(TransformField::Rotation));

        ScenePlacement target, follower;
        target.id = "target";
        target.position = {4, 5, 6};
        follower.id = "follower";
        follower.scripts.push_back({"follow", true, "FollowTarget", {}});
        follower.scripts.back().data["target"] = {ScriptObjectReference{target.id}};
        follower.scripts.back().data["offset"] = {ScriptValue::Object{{"x", {1.0f}}, {"y", {-2.0f}}, {"z", {3.0f}}}};
        SceneLayout follow;
        follow.objects = {target, follower};
        ScriptRuntime runtime;
        contract("capture-follow-target",
                 runtime.Update(follow, .016, error) && follow.objects[1].position == std::array<float, 3>{5, 3, 9});
        follow.objects[1].scripts[0].data["active"] = {false};
        follow.objects[0].position[0] += 1;
        contract("capture-follow-disabled",
                 runtime.Update(follow, .016, error) && follow.objects[1].position == std::array<float, 3>{5, 3, 9});
        follow.objects[1].scripts[0].data["active"] = {true};
        follow.objects[1].parentId = target.id;
        const auto invalidParents = follow.Serialize();
        contract("capture-follow-parent-failure",
                 !runtime.Update(follow, .016, error) && follow.Serialize() == invalidParents);
        runtime.Stop(follow);
        CompleteChangeCaptureValidation::Data(root, contract);
    }

    /// <summary>Scriptの新旧経路と、固定更新・LateUpdate・資源準備失敗時の通知を検証します。</summary>
    template <class Contract>
    void Runtime(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& root, Contract contract)
    {
        using namespace SceneRuntime;
        bool failUpdate = false, failLate = false, failResources = false, drainedDuringPreparation = false;
        bool useTypedApi = true;
        SceneWorld world;
        ScriptDefinition definition;
        definition.update = [&](ScriptContext& context)
        {
            auto transform = *context.scene->Read().Transform(context.object.id);
            transform.position[0] += 1;
            if (useTypedApi)
            {
                if (!context.SetLocalTransform(transform).succeeded)
                    throw std::runtime_error("typed script transform rejected");
            }
            else
                context.object.position = transform.position;
            drainedDuringPreparation |= !world.TakeChanges().Empty();
            if (failUpdate)
                throw std::runtime_error("injected update failure");
        };
        definition.fixedUpdate = [](ScriptContext& context)
        {
            auto transform = *context.scene->Read().Transform(context.object.id);
            transform.position[1] += 1;
            if (!context.SetLocalTransform(transform).succeeded)
                throw std::runtime_error("typed fixed transform rejected");
        };
        definition.lateUpdate = [&](ScriptContext& context)
        {
            auto transform = *context.scene->Read().Transform(context.object.id);
            transform.rotation[1] += .25f;
            if (!context.SetLocalTransform(transform).succeeded || failLate)
                throw std::runtime_error("injected late failure");
        };
        contract("capture-register-script", ScriptRegistry::Register("CaptureTransform", definition));
        SceneLayout layout;
        ScenePlacement owner;
        owner.id = "actor";
        owner.scripts.push_back({"capture", true, "CaptureTransform", {}});
        layout.objects = {owner};
        std::string error;
        contract("capture-runtime-setup",
                 world.Initialize(renderer, root, layout, root / "Engine/Shaders/Mesh.hlsl", &error) &&
                     world.StartScripts(error));
        world.TakeChanges();
        world.SetRuntimePreparation(
            [&](const SceneLayout&, std::string&)
            {
                return !failResources;
            });
        const auto original = world.Layout().Serialize();
        for (int failure = 0; failure < 3; ++failure)
        {
            failUpdate = failure == 0;
            failLate = failure == 1;
            failResources = failure == 2;
            contract("capture-frame-rollback-" + std::to_string(failure),
                     !world.UpdateFrame(1.0 / 60, 0, 0, false, true) && world.Layout().Serialize() == original &&
                         world.TakeChanges().Empty());
        }
        failUpdate = failLate = failResources = false;
        contract("capture-frame-success", world.UpdateFrame(1.0 / 60, 0, 0, false, true));
        auto published = world.TakeChanges();
        const auto typedState = world.Read().Transform(owner.id);
        contract("capture-frame-fields", published.transforms.size() == 1 &&
                                             published.transforms[0].Has(TransformField::Position) &&
                                             published.transforms[0].Has(TransformField::Rotation) &&
                                             !published.transforms[0].Has(TransformField::Scale) &&
                                             !published.transforms[0].Has(TransformField::Parent) &&
                                             typedState->position == std::array<float, 3>{1, 1, 0} &&
                                             typedState->rotation[1] == .25f && world.TakeChanges().Empty());
        contract("capture-runtime-reset", world.ReplaceLayout(layout, root, error) && world.StartScripts(error));
        world.TakeChanges();
        useTypedApi = false;
        contract("capture-script-legacy-parity",
                 world.UpdateFrame(1.0 / 60, 0, 0, false, true) && world.Read().Transform(owner.id) == typedState);
        failResources = true;
        contract("capture-runtime-keeps-unread", !world.UpdateFrame(1.0 / 60, 0, 0, false, true));
        published = world.TakeChanges();
        contract("capture-runtime-no-early-drain", !drainedDuringPreparation && world.TakeChanges().Empty() &&
                                                       published.transforms.size() == 1 &&
                                                       published.transforms[0].Has(TransformField::Position) &&
                                                       published.transforms[0].Has(TransformField::Rotation));
        CompleteChangeCaptureValidation::Runtime(renderer, root, contract);
    }
}
