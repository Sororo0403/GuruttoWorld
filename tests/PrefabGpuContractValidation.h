#pragma once
#include <SceneRuntime/Prefab.h>
#include <SceneRuntime/SceneWorld.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <SceneRuntime/SceneEnvironment.h>
#include "../Editor/src/PrefabEditPlan.h"
#include "../Editor/src/GameSession.h"
#include "../Editor/src/SceneDocument.h"

namespace PrefabGpuContractValidation
{
    /// <summary>旧参照を持つシーンを修復用に開けることと、実行では拒否することを検証します。</summary>
    inline void Run(const Engine::DirectX12Renderer& renderer)
    {
        using namespace SceneRuntime;
        const auto require = [](bool ok, const char* message)
        {
            if (!ok)
                throw std::runtime_error(message);
        };
        const auto root = std::filesystem::absolute("generated/tests/prefab-gpu") /
                          (std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
        std::filesystem::create_directories(root / "Assets/Prefabs");
        std::filesystem::create_directories(root / "Assets/Scenes");
        ScenePlacement owner, external;
        owner.id = "owner";
        owner.scripts.emplace_back();
        owner.scripts.front().enabled = false;
        owner.scripts.front().data["target"] = {ScriptObjectReference{"external"}};
        external.id = "external";
        SceneLayout asset;
        asset.objects = {owner};
        const auto assetPath = std::filesystem::path("Assets/Prefabs/Legacy.prefab");
        asset.Save(root / assetPath);
        SceneLayout scene;
        scene.objects = {external};
        const auto id = Prefab::Instantiate(scene, asset, assetPath, {});
        scene.objects.back().prefab->referenceVersion = 0;
        scene.objects.back().prefab->externalBindings.clear();
        scene.objects.back().scripts.front().data["target"] = {ScriptObjectReference{external.id}};
        const auto scenePath = root / "Assets/Scenes/Legacy.json";
        scene.Save(scenePath);
        const auto before = SceneLayout::Load(scenePath, root).Serialize();
        const auto shader = std::filesystem::absolute("Content/Shaders/Mesh.hlsl");
        std::string error;
        SceneWorld strict;
        require(!strict.Initialize(renderer, root, scenePath, shader, &error) &&
                    error.find("explicit binding") != std::string::npos,
                "Execution must reject ambiguous legacy prefab references");
        SceneWorld editing;
        require(editing.Initialize(renderer, root, scenePath, shader, &error, true) &&
                    error.find("explicit binding") != std::string::npos && editing.Layout().Serialize() == before,
                "Editing must preserve ambiguous legacy data and report migration");
        auto migrated = editing.Layout();
        Prefab::BindReferences(migrated, id, {{"external", {""}}});
        Prefab::Refresh(migrated, root);
        require(editing.ReplaceLayout(migrated, root, error), "Explicit binding applies through ordinary editing");
        const auto& reference =
            std::get<ScriptObjectReference>(migrated.objects.back().scripts.front().data.at("target").value);
        require(reference.id.empty(), "An unrelated object with the same ID remains unbound");
        const auto current = editing.Layout().Serialize();
        std::filesystem::remove(root / assetPath);
        require(!editing.Reload(root, scenePath, error, true) && editing.Layout().Serialize() == current,
                "Editing migration permission must not hide unrelated load failures");

        SceneLayout soundScene;
        ScenePlacement sound;
        sound.id = "sound";
        sound.audioSource.emplace().playOnAwake = false;
        sound.scripts.emplace_back().behaviour = "ManagedGpuAudio";
        sound.button.emplace().sound = "sound";
        soundScene.objects = {sound};
        bool fail = true;
        ScriptDefinition definition;
        definition.update = [&](ScriptContext& context)
        {
            context.scene->PlayAudio(context.object.id);
            context.scene->AudioCue("confirm");
            if (fail)
                context.object.SetModel("Assets/Models/Missing.gltf");
        };
        require(ScriptRegistry::Register("ManagedGpuAudio", definition), "Register managed GPU audio");
        Engine::FileContents::Write(
            root / "WP1.project.json",
            Engine::Json{{"engineRoot", Engine::ProjectContext::SourceEngineRoot().generic_string()}}.dump());
        SceneEnvironment environment;
        if (!environment.Initialize(renderer, root, soundScene, error))
            throw std::runtime_error("Initialize managed audio environment: " + error);
        unsigned int played = 0;
        environment.SetAudioCommandHandler(
            [&](const AudioCommand&)
            {
                ++played;
            });
        const auto soundBefore = environment.World().Layout().Serialize();
        environment.AudioCue("before-failure");
        environment.Click("sound");
        require(!environment.Update(.016, false, true), "Resource failure rejects runtime frame");
        environment.CommitAudioCommands();
        require(played == 0 && environment.World().Layout().Serialize() == soundBefore,
                "GPU failure must discard all audio and preserve world");
        fail = false;
        require(environment.Update(.016, false, true), "Valid frame recovers");
        environment.CommitAudioCommands();
        environment.CommitAudioCommands();
        require(played == 2, "Committed audio executes exactly once without failed-frame cues");
        environment.AudioCue("paused");
        environment.DiscardPendingInput();
        require(environment.Update(.016, false, true), "Resume valid audio frame");
        environment.CommitAudioCommands();
        require(played == 4, "Pause discards pending cue");
        environment.AudioCue("invalid-time");
        require(!environment.Update(0, false, true), "Invalid time skips update");
        environment.CommitAudioCommands();
        require(played == 4, "Skipped frame cannot commit audio");
        environment.Click("sound");
        require(environment.Update(.016, false, true), "Click audio valid frame");
        environment.CommitAudioCommands();
        require(played == 7, "Click audio is deferred until successful update");

        SceneLayout managedAsset;
        ScenePlacement managed;
        managed.id = "managed";
        managed.name = "Before";
        managedAsset.objects = {managed};
        const auto managedPath = root / "Assets/Prefabs/Managed.prefab";
        managedAsset.Save(managedPath);
        SceneLayout managedScene;
        const auto managedId = Prefab::Instantiate(managedScene, managedAsset, "Assets/Prefabs/Managed.prefab", {});
        SceneWorld managedWorld;
        require(managedWorld.Initialize(renderer, root, managedScene, shader, &error),
                "Initialize managed prefab world");
        managedScene = managedWorld.Layout();
        managedScene.objects.front().name = "After";
        auto plan = Editor::PrefabEdits::Override(managedScene, managedId, "/name", root);
        const auto assetBefore = Engine::FileContents::Read(managedPath);
        const auto metaBefore = Engine::FileContents::Read(Engine::AssetDatabase::Sidecar(managedPath));
        const auto worldBefore = managedWorld.Layout().Serialize();
        auto invalid = plan.scene;
        invalid.objects.front().SetModel("Assets/Models/Missing.gltf");
        require(!Editor::FileTransaction::Apply(plan.files, true, error,
                                                [&]
                                                {
                                                    return managedWorld.ReplaceLayout(invalid, root, error);
                                                }) &&
                    managedWorld.Layout().Serialize() == worldBefore &&
                    Engine::FileContents::Read(managedPath) == assetBefore &&
                    Engine::FileContents::Read(Engine::AssetDatabase::Sidecar(managedPath)) == metaBefore,
                "Failed GPU preparation restores prefab and metadata");
        require(Editor::FileTransaction::Apply(plan.files, true, error,
                                               [&]
                                               {
                                                   return managedWorld.ReplaceLayout(plan.scene, root, error);
                                               }),
                "Managed commit after recovery");
        require(Editor::FileTransaction::Apply(plan.files, false, error,
                                               [&]
                                               {
                                                   return managedWorld.ReplaceLayout(managedScene, root, error);
                                               }) &&
                    managedWorld.Layout().Serialize() == managedScene.Serialize() &&
                    Engine::FileContents::Read(managedPath) == assetBefore,
                "Managed undo commits scene and asset together");

        Editor::SceneDocument ownedDocument(root / "Assets/Scenes/Owned.json", environment.Content());
        SceneWorld documentView(ownedDocument.Data());
        SceneLayout missingDocument;
        ScenePlacement missingObject;
        missingObject.id = "missing";
        missingObject.SetModel("Assets/Models/Missing.gltf");
        missingObject.animator.emplace();
        missingDocument.objects = {missingObject};
        const auto missingBefore = missingDocument.Serialize();
        require(documentView.Initialize(renderer, root, missingDocument, shader, &error) &&
                    documentView.IsDocumentView() && !documentView.ResourceDiagnostics().empty() &&
                    ownedDocument.Layout().Serialize() == missingBefore,
                "Document retains missing model and animator without execution state");
        require(!documentView.UpdateComponents(.016) && !documentView.MovePlayers(.016, 0, 0),
                "Editing view cannot own runtime updates");
        auto repaired = missingDocument;
        repaired.objects.front().meshRenderer.reset();
        repaired.objects.front().animator.reset();
        require(documentView.ReplaceLayout(repaired, root, error) && documentView.ResourceDiagnostics().empty(),
                "Missing resources can be repaired");
        require(documentView.ReplaceLayout(missingDocument, root, error) &&
                    ownedDocument.Layout().Serialize() == missingBefore && !documentView.ResourceDiagnostics().empty(),
                "Undo to missing resources preserves document");
        auto invalidDocument = missingDocument;
        invalidDocument.objects.front().scale = {0, 1, 1};
        require(!documentView.ReplaceLayout(invalidDocument, root, error) &&
                    ownedDocument.Layout().Serialize() == missingBefore,
                "Invalid data remains rejected in repair mode");
        std::vector<std::string> duplicates;
        require(documentView.DuplicateObjects({"missing"}, {}, duplicates, error) && duplicates.size() == 1,
                "Missing animated objects can be duplicated without a GPU model");

        Editor::GameSession session;
        SceneLayout playable;
        playable.objects = {sound};
        const auto editBefore = ownedDocument.Layout().Serialize();
        require(!session.Play(renderer, root, playable, error) && session.State().IsEditing() && !session.Runtime(),
                "Late playback settings failure must not publish a runtime");
        fail = false;
        require(session.Play(renderer, root, playable, error, root / "settings.json", environment.Content()),
                "Play publishes fully prepared runtime");
        require(session.Update(.016, true), "Valid playback advances");
        require(session.Pause(), "Pause active playback");
        const auto elapsed = session.State().Elapsed();
        const auto updates = session.State().Updates();
        fail = true;
        require(!session.Step() && session.State().Elapsed() == elapsed && session.State().Updates() == updates,
                "Failed Step preserves playback clock");
        fail = false;
        require(session.Step() && session.State().Updates() == updates + 1 && session.State().CanStep(),
                "Successful Step advances once and remains paused");
        const auto previousRuntime = session.Runtime();
        require(!session.LoadScene(renderer, root, invalidDocument, error) && session.Runtime() == previousRuntime &&
                    session.State().CanStep(),
                "Failed scene preparation preserves paused session");
        require(session.Stop() && session.State().IsEditing() && !session.Runtime() &&
                    ownedDocument.Layout().Serialize() == editBefore,
                "Stop discards runtime without rebuilding or restoring editing document");

        int startMode = 1, startResources = 0, starts = 0, stops = 0, normalUpdates = 0;
        ScriptDefinition starter;
        starter.start = [&](ScriptContext& current)
        {
            ++starts;
            ++startResources;
            if (startMode == 1)
                throw std::runtime_error("Injected start failure");
            if (startMode == 2)
                current.object.SetModel("Assets/Models/Missing.gltf");
            if (startMode == 3)
            {
                current.object.rectTransform.emplace();
                current.object.image.emplace().texture = "Assets/Textures/Missing.png";
            }
        };
        starter.stop = [&](ScriptContext&)
        {
            ++stops;
            --startResources;
        };
        starter.update = [&](ScriptContext&)
        {
            ++normalUpdates;
        };
        require(ScriptRegistry::Register("PreparedGpuStart", starter), "Register prepared start");
        SceneLayout startLayout;
        ScenePlacement startOwner;
        startOwner.id = "start-owner";
        startOwner.scripts.emplace_back().behaviour = "PreparedGpuStart";
        startLayout.objects = {startOwner};
        for (startMode = 1; startMode <= 3; ++startMode)
            require(!session.Play(renderer, root, startLayout, error, root / "settings.json", environment.Content()) &&
                        session.State().IsEditing() && !session.Runtime() && startResources == 0 && starts == stops &&
                        ownedDocument.Layout().Serialize() == editBefore,
                    "Start, model and UI failures must preserve Editing and release candidate resources");
        startMode = 0;
        require(session.Play(renderer, root, startLayout, error, root / "settings.json", environment.Content()) &&
                    startResources == 1 && normalUpdates == 0 && session.State().Updates() == 0,
                "Play prepares start without normal update or clock advancement");
        require(session.Stop() && startResources == 0 && starts == stops,
                "Stop releases successfully prepared script resources once");

        const auto declaration = Engine::FileContents::Read(root / "WP1.project.json");
        const auto stableRuntime = environment.World().Layout().Serialize();
        Engine::FileContents::Write(root / "WP1.project.json",
                                    Engine::Json{{"engineRoot", (root / "different-engine").generic_string()}}.dump());
        require(!environment.World().ReplaceLayout(soundScene, root, error) &&
                    environment.World().Layout().Serialize() == stableRuntime &&
                    error.find("reopen") != std::string::npos,
                "Changed declaration cannot publish resources from a different engine");
        require(documentView.ReplaceLayout(repaired, root, error) && !documentView.ResourceDiagnostics().empty(),
                "Editing data remains repairable while changed declaration blocks resource creation");
        Engine::FileContents::Write(root / "WP1.project.json", *declaration);
    }
}
