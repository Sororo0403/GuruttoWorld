#pragma once
#include <SceneRuntime/Scene/SceneDocumentData.h>
#include <SceneRuntime/Scene/SceneWorld.h>
#include <SceneRuntime/Scene/SceneCollection.h>
#include <SceneRuntime/Scene/SceneAssetReferences.h>
#include <SceneRuntime/Scripts/ScriptRuntime.h>
#include <SceneRuntime/Project/GameSettings.h>
#include <Engine/Core/FileContents.h>

namespace SessionDesignContractValidation
{
    /// <summary>参照情報の固定と、未解決参照の範囲をCPUだけで検証します。</summary>
    template <class Contract> void Run(const std::filesystem::path& root, Contract contract)
    {
        using namespace SceneRuntime;
        using Engine::FileContents;
        std::filesystem::create_directories(root / "Assets/Models");
        std::filesystem::create_directories(root / "Shaders");
        const auto identity = std::string(32, 'c');
        const auto descriptor = root / "WP1.project.json";
        FileContents::Write(descriptor,
                            Engine::Json{{"engineRoot", Engine::ProjectContext::SourceEngineRoot().generic_string()},
                                         {"projectId", identity}}
                                .dump());
        FileContents::Write(root / "Assets/Project.json", "{}");
        const auto context = Engine::ContentContext::Capture(root);
        const auto shader = context->Resolve("Shaders/Mesh.hlsl");
        FileContents::Write(descriptor, Engine::Json{{"engineRoot", (root / "missing-engine").generic_string()},
                                                     {"projectId", std::string(32, 'd')}}
                                            .dump());
        contract("context-frozen-layers", context->Resolve("Shaders/Mesh.hlsl") == shader &&
                                              std::filesystem::is_regular_file(shader) &&
                                              context->Layers().size() == 2);
        std::string declarationError;
        contract("context-declaration-change-guard", !context->CheckConfiguration(declarationError) &&
                                                         declarationError.find("reopen") != std::string::npos);
        contract("context-frozen-settings-id",
                 GameSettings::UserPath(*context, root / "user") == root / "user/games" / identity / "settings.json");
        bool wrongRoot = false;
        try
        {
            context->RequireRoot(root / "other");
        }
        catch (const std::exception&)
        {
            wrongRoot = true;
        }
        contract("context-rejects-root-mixing", wrongRoot);
        const auto index = Engine::AssetDatabase(context, true);
        contract("context-index-keeps-layers",
                 index.Resolve("Shaders/Mesh.hlsl") == std::filesystem::path("Shaders/Mesh.hlsl") &&
                     !index.ReferenceId("Shaders/Mesh.hlsl").empty());

        SceneLayout missing;
        ScenePlacement object;
        object.id = "owner";
        object.SetModel("Assets/Models/Missing.gltf");
        object.scripts.emplace_back().enabled = false;
        object.scripts.back().data["target"] = {ScriptObjectReference{"external"}};
        missing.objects = {object};
        missing.assetReferences["Assets/Models/Missing.gltf"] = std::string(32, 'e');
        const auto before = missing.Serialize();
        SceneDocumentData document(missing, context);
        contract("document-without-renderer", document.Layout().Serialize() == before && document.Content() == context);
        auto owned = std::make_shared<SceneDocumentData>(missing, context);
        SceneWorld view(owned);
        std::string viewError;
        contract("document-headless-edit", view.ReplaceLayout(missing, root, viewError) &&
                                               owned->Layout().Serialize() == before &&
                                               !view.ResourceDiagnostics().empty());
        bool competingView = false;
        try
        {
            SceneWorld secondView(owned);
        }
        catch (const std::exception&)
        {
            competingView = true;
        }
        contract("document-single-projection-owner", competingView && owned->Layout().Serialize() == before);
        auto repaired = missing;
        repaired.objects.front().meshRenderer.reset();
        contract("document-headless-repair",
                 view.ReplaceLayout(repaired, root, viewError) && !owned->Layout().objects.front().meshRenderer);
        contract("document-headless-undo", view.ReplaceLayout(missing, root, viewError) &&
                                               owned->Layout().Serialize() == before && !view.UpdateComponents(.016));
        const auto diagnostics = ResolveSceneAssetReferencesForEditing(missing, index);
        contract("document-missing-id-preserved", !diagnostics.empty() && missing.Serialize() == before);

        SceneLayout main, incoming;
        ScenePlacement external;
        external.id = "external";
        main.objects = {external};
        object.meshRenderer.reset();
        incoming.objects = {object};
        SceneCollection collection;
        collection.Reset(main);
        auto merged = collection.Add(main, incoming, "incoming");
        const auto target =
            std::get<ScriptObjectReference>(merged.objects.back().scripts.front().data.at("target").value).id;
        contract("scene-missing-reference-not-captured", target != "external" && !target.empty() &&
                                                             std::none_of(merged.objects.begin(), merged.objects.end(),
                                                                          [&](const auto& member)
                                                                          {
                                                                              return member.id == target;
                                                                          }));
        SceneLayout later;
        external.id = target;
        later.objects = {external};
        merged = collection.Add(merged, later, "later");
        contract("scene-missing-reference-stays-unresolved", merged.objects.back().id != target);

        SceneLayout spawn;
        object.scripts.front().data["target"] = {ScriptObjectReference{"runtime-1"}};
        spawn.objects = {object};
        size_t next = 1;
        ScriptScene api(spawn, next);
        const auto spawned = api.Spawn(ScenePlacement{});
        api.Commit();
        contract("runtime-spawn-reserves-missing-reference", spawned != "runtime-1");

        unsigned int callbacks = 0;
        bool correctContext = true;
        ScriptDefinition definition;
        const auto observe = [&](const auto& current)
        {
            ++callbacks;
            correctContext = correctContext && current.content == context && current.contentRoot == context->Root();
        };
        definition.start = observe;
        definition.update = observe;
        definition.stop = observe;
        definition.pause = [&](ScriptContext& current, bool)
        {
            observe(current);
        };
        ScriptRegistry::Register("OwnedContextLifecycle", definition);
        SceneLayout runtimeLayout;
        object.scripts.front().behaviour = "OwnedContextLifecycle";
        object.scripts.front().enabled = true;
        runtimeLayout.objects = {object};
        ScriptRuntime runtime;
        runtime.SetContentContext(context);
        std::string error;
        const bool updated = runtime.Update(runtimeLayout, .016, error);
        runtime.Pause(runtimeLayout, true);
        runtime.Stop(runtimeLayout);
        contract("context-all-lifecycle-callbacks", updated && callbacks == 4 && correctContext);
        object.scripts.front().behaviour = "MissingPreflightBehaviour";
        runtimeLayout.objects = {object};
        contract("play-preflight-unknown-script", !ScriptRuntime::ValidateForPlay(runtimeLayout, error));
        runtimeLayout.objects.front().scripts.front().enabled = false;
        contract("play-preflight-preserves-disabled-script", ScriptRuntime::ValidateForPlay(runtimeLayout, error));
        int startResources = 0, startCalls = 0, updateCalls = 0;
        ScriptDefinition starter;
        starter.start = [&](ScriptContext& current)
        {
            ++startResources;
            ++startCalls;
            current.object.position[0] = 42;
        };
        starter.stop = [&](const ScriptStopContext&)
        {
            --startResources;
        };
        starter.update = [&](ScriptContext&)
        {
            ++updateCalls;
        };
        ScriptRegistry::Register("PreparedStartContract", starter);
        runtimeLayout.objects.front().scripts.front().behaviour = "PreparedStartContract";
        runtimeLayout.objects.front().scripts.front().enabled = true;
        const auto unstarted = runtimeLayout.Serialize();
        {
            auto candidate = runtimeLayout;
            ScriptRuntime pending;
            contract("play-start-candidate-only", pending.StartCandidate(candidate, error) && startResources == 1 &&
                                                      startCalls == 1 && updateCalls == 0 &&
                                                      candidate.objects.front().position[0] == 42 &&
                                                      runtimeLayout.Serialize() == unstarted);
        }
        contract("play-start-candidate-cleanup", startResources == 0);
    }
}
