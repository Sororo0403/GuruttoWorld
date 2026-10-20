#pragma once
#include "../Editor/src/PrefabEditPlan.h"
#include "../Editor/src/EditHistory.h"
#include <SceneRuntime/ScriptRuntime.h>

namespace EditTransactionContractValidation
{
    /// <summary>素材・履歴・音声予約の確定と、失敗時の原状維持を検証します。</summary>
    template <class Contract> void Run(const std::filesystem::path& root, Contract contract)
    {
        using namespace SceneRuntime;
        using namespace Editor;
        using Engine::FileContents;
        std::filesystem::create_directories(root / "Assets/Prefabs");
        const auto path = root / "Assets/Prefabs/Managed.prefab";
        SceneLayout asset;
        ScenePlacement owner;
        owner.id = "owner";
        owner.name = "Original";
        asset.objects.push_back(owner);
        asset.Save(path);
        auto metadata = Engine::AssetDatabase::CreateMetadata();
        auto rawMetadata = Engine::Json::parse(Engine::AssetDatabase::SerializeMetadata(metadata));
        rawMetadata["userNote"] = "Preserve exactly";
        const auto meta = Engine::AssetDatabase::Sidecar(path);
        FileContents::Write(meta, rawMetadata.dump(3));
        SceneLayout scene;
        const auto id = Prefab::Instantiate(scene, asset, "Assets/Prefabs/Managed.prefab", {});
        scene.ResolveAssets(root);
        EditHistory history;
        history.Reset({scene.Serialize(), id});
        scene.objects.front().name = "Local rename";
        history.Observe({scene.Serialize(), id}, {});
        const auto beforeScene = scene.Serialize();
        const auto beforeFile = FileContents::Read(path), beforeMeta = FileContents::Read(meta);
        auto plan = PrefabEdits::Override(scene, id, "/name", root);
        contract("edit-plan-no-writes", scene.Serialize() == beforeScene && FileContents::Read(path) == beforeFile &&
                                            FileContents::Read(meta) == beforeMeta);
        std::string error;
        const auto apply = [&](const std::vector<FileChange>& files, bool forward, const SceneLayout& document)
        {
            return FileTransaction::Apply(files, forward, error,
                                          [&]
                                          {
                                              scene = document;
                                              return true;
                                          });
        };
        const bool applied = apply(plan.files, true, plan.scene);
        history.RecordTransaction({beforeScene, id}, {scene.Serialize(), id}, plan.files);
        const auto afterFile = FileContents::Read(path);
        const auto afterScene = scene.Serialize();
        contract("edit-prefab-commit", applied && SceneLayout::Load(path).objects.front().name == "Local rename" &&
                                           FileContents::Read(meta) == beforeMeta);
        auto target = SceneLayout::Parse(history.Target(false).json);
        bool undone = apply(history.TargetFiles(false), false, target);
        if (undone)
            history.Applied(false);
        auto refreshed = scene;
        Prefab::Refresh(refreshed, root);
        contract("prefab-asset-undo", undone && FileContents::Read(path) == beforeFile &&
                                          FileContents::Read(meta) == beforeMeta && scene.Serialize() == beforeScene &&
                                          refreshed.Serialize() == beforeScene &&
                                          scene.objects.front().name == "Local rename");
        target = SceneLayout::Parse(history.Target(true).json);
        bool redone = apply(history.TargetFiles(true), true, target);
        if (redone)
            history.Applied(true);
        contract("edit-prefab-redo",
                 redone && FileContents::Read(path) == afterFile && scene.Serialize() == afterScene);
        FileContents::Write(meta, *beforeMeta + "\n");
        const auto externalMeta = FileContents::Read(meta);
        target = SceneLayout::Parse(history.Target(false).json);
        contract("edit-undo-external-meta",
                 !apply(history.TargetFiles(false), false, target) && FileContents::Read(path) == afterFile &&
                     FileContents::Read(meta) == externalMeta && scene.Serialize() == afterScene && history.CanUndo());
        FileContents::Write(meta, *beforeMeta);
        FileContents::Write(path, *afterFile + "\n");
        contract("edit-undo-external-asset", !apply(history.TargetFiles(false), false, target) &&
                                                 scene.Serialize() == afterScene &&
                                                 FileContents::Read(path) == *afterFile + "\n");
        FileContents::Write(path, *afterFile);

        const auto created = root / "Assets/Prefabs/Created.prefab";
        auto creation = PrefabEdits::Save(scene, id, "Assets/Prefabs/Created.prefab", root, false, false);
        const auto createdMeta = Engine::AssetDatabase::Sidecar(created);
        contract("edit-create-plan-pure", !FileContents::Read(created) && !FileContents::Read(createdMeta));
        bool createdOk = apply(creation.files, true, creation.scene);
        const auto createdBytes = FileContents::Read(created), createdId = FileContents::Read(createdMeta);
        createdOk = createdOk && apply(creation.files, false, SceneLayout::Parse(afterScene)) &&
                    !FileContents::Read(created) && !FileContents::Read(createdMeta);
        createdOk = createdOk && apply(creation.files, true, creation.scene);
        contract("edit-create-undo-redo-id", createdOk && FileContents::Read(created) == createdBytes &&
                                                 FileContents::Read(createdMeta) == createdId);

        const auto a = root / "a.txt", b = root / "b.txt";
        FileContents::Write(a, "A");
        FileContents::Write(b, "B");
        std::vector<FileChange> files{FileTransaction::Prepare(a, "A2"), FileTransaction::Prepare(b, "B2")};
        bool called = false;
        contract("edit-document-rejection-rolls-files", !FileTransaction::Apply(files, true, error,
                                                                                [&]
                                                                                {
                                                                                    called = true;
                                                                                    return false;
                                                                                }) &&
                                                            called && FileContents::Read(a) == "A" &&
                                                            FileContents::Read(b) == "B");
        contract("edit-document-exception-rolls-files",
                 !FileTransaction::Apply(files, true, error,
                                         []() -> bool
                                         {
                                             throw std::runtime_error("injected");
                                         }) &&
                     FileContents::Read(a) == "A" && FileContents::Read(b) == "B");
        SetFileAttributesW(b.c_str(), FILE_ATTRIBUTE_READONLY);
        called = false;
        const bool rejectedWrite = !FileTransaction::Apply(files, true, error,
                                                           [&]
                                                           {
                                                               called = true;
                                                               return true;
                                                           });
        SetFileAttributesW(b.c_str(), FILE_ATTRIBUTE_NORMAL);
        contract("edit-partial-write-rollback",
                 rejectedWrite && !called && FileContents::Read(a) == "A" && FileContents::Read(b) == "B");
        bool externalRejected = false;
        const bool externalRollback = !FileTransaction::Apply(files, true, error,
                                                              [&]
                                                              {
                                                                  try
                                                                  {
                                                                      FileContents::Write(a, "external");
                                                                  }
                                                                  catch (const std::exception&)
                                                                  {
                                                                      externalRejected = true;
                                                                  }
                                                                  return false;
                                                              });
        // 取引中の外部編集は拒否し、保存候補をすべて元に戻します。
        contract("edit-rollback-protects-external-change", externalRollback && externalRejected &&
                                                               FileContents::Read(a) == "A" &&
                                                               FileContents::Read(b) == "B" && !error.empty());
        FileContents::Write(a, "A");
        auto duplicate = files;
        duplicate.push_back(files.front());
        contract("edit-duplicate-target", !FileTransaction::Apply(duplicate, true, error,
                                                                  []
                                                                  {
                                                                      return true;
                                                                  }) &&
                                              FileContents::Read(a) == "A");
        auto stream = files;
        stream.front().path += L":hidden";
        contract("edit-stream-target", !FileTransaction::Apply(stream, true, error,
                                                               []
                                                               {
                                                                   return true;
                                                               }) &&
                                           FileContents::Read(a) == "A");
        std::vector<FileChange> folder{{root, {}, "invalid"}};
        contract("edit-folder-target", !FileTransaction::Apply(folder, true, error,
                                                               []
                                                               {
                                                                   return true;
                                                               }) &&
                                           std::filesystem::is_directory(root));

        EditHistory same;
        same.Reset({"same", "one"});
        same.RecordTransaction({"same", "one"}, {"same", "two"}, files);
        contract("edit-asset-only-history",
                 same.CanUndo() && same.Target(false).json == "same" && same.TargetFiles(false).size() == 2);
        same.Applied(false);
        same.Observe({"branch", "one"}, {});
        contract("edit-history-truncates-file-redo", !same.CanRedo() && same.TargetFiles(false).empty());
        for (int i = 0; i < 110; ++i)
            same.RecordTransaction({std::to_string(i), ""}, {std::to_string(i + 1), ""}, files);
        unsigned int steps = 0;
        while (same.CanUndo())
        {
            same.Applied(false);
            ++steps;
        }
        contract("edit-history-bounded", steps == 100 && same.CanRedo());

        std::filesystem::create_directories(root / "Assets/Models");
        const auto original = root / "Assets/Models/Old.gltf", moved = root / "Assets/Models/Moved.gltf";
        FileContents::Write(original, "{}");
        const auto originalMetadata = Engine::AssetDatabase::Ensure(original);
        SceneLayout referenced;
        owner.SetModel("Assets/Models/Old.gltf");
        owner.scripts.clear();
        owner.audioSource.reset();
        referenced.objects = {owner};
        referenced.assetReferences["Assets/Models/Old.gltf"] = originalMetadata.id;
        std::filesystem::rename(original, moved);
        std::filesystem::rename(Engine::AssetDatabase::Sidecar(original), Engine::AssetDatabase::Sidecar(moved));
        FileContents::Write(original, "{}");
        Engine::AssetDatabase::Ensure(original);
        const auto captured = SceneLayout::Parse(referenced.SerializeForSave(root / "Assets/Scenes/Reference.json"));
        contract("edit-save-known-id-authority",
                 captured.assetReferences.at("Assets/Models/Old.gltf") == originalMetadata.id);
        auto extractedReference = Prefab::Extract(referenced, owner.id);
        extractedReference.Save(root / "Assets/Prefabs/References.prefab");
        const auto resolved = SceneLayout::Load(root / "Assets/Prefabs/References.prefab", root);
        contract("edit-extract-id-closure",
                 resolved.objects.front().Model() == "Assets/Models/Moved.gltf" &&
                     resolved.assetReferences.at("Assets/Models/Moved.gltf") == originalMetadata.id);
        SceneLayout linked;
        Prefab::Instantiate(linked, extractedReference, "Assets/Prefabs/References.prefab", {});
        const auto referenceBaseline = SceneLayout::Parse(linked.objects.front().prefab->baseline);
        contract("edit-baseline-id-closure",
                 referenceBaseline.assetReferences.at("Assets/Models/Old.gltf") == originalMetadata.id);
        bool badMetadata = false;
        try
        {
            Engine::AssetDatabase::ParseMetadata("{\"id\":\"invalid\"}");
        }
        catch (const std::exception&)
        {
            badMetadata = true;
        }
        contract("edit-metadata-validation",
                 badMetadata &&
                     Engine::AssetDatabase::ParseMetadata("{\"id\":\"" + metadata.id + "\"}").id == metadata.id);
        owner.meshRenderer.reset();

        int mode = 0;
        ScriptDefinition definition;
        definition.update = [&](ScriptContext& context)
        {
            context.scene->PlayAudio(context.object.id);
            context.scene->AudioCue("confirm");
            if (mode == 1)
                throw std::runtime_error("injected");
            if (mode == 2)
                context.object.scale = {0, 1, 1};
            if (mode == 3)
                context.scene->Destroy(context.object.id);
            if (mode == 4)
                for (int i = 0; i < 65; ++i)
                    context.scene->AudioCue("overflow");
        };
        ScriptRegistry::Register("ManagedAudioContract", definition);
        SceneLayout audio;
        owner.audioSource.emplace();
        owner.audioSource->playOnAwake = false;
        owner.scripts.emplace_back();
        owner.scripts.back().behaviour = "ManagedAudioContract";
        audio.objects = {owner};
        ScriptRuntime runtime;
        const auto audioBefore = audio.Serialize();
        for (int failure = 1; failure <= 4; ++failure)
        {
            mode = failure;
            contract("audio-failure-" + std::to_string(failure), !runtime.Update(audio, .016, error) &&
                                                                     runtime.TakeAudioCommands().empty() &&
                                                                     audio.Serialize() == audioBefore);
        }
        mode = 0;
        const bool success = runtime.Update(audio, .016, error);
        const auto commands = runtime.TakeAudioCommands();
        contract("audio-commands-once",
                 success && commands.size() == 2 && commands.front().kind == AudioCommandKind::Play &&
                     commands.back().kind == AudioCommandKind::Cue && runtime.TakeAudioCommands().empty());
        runtime.Update(audio, .016, error);
        runtime.Stop(audio);
        contract("audio-stop-discards", runtime.TakeAudioCommands().empty());
    }
}
