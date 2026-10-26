#pragma once
#include <LegacyScripts/ScriptRuntime.h>
#include <SceneRuntime/Scene/SceneMigration.h>
#include "../../../Editor/src/Documents/SceneSnapshotCache.h"
#include "../../../Editor/src/Editing/EditHistory.h"
#include <fstream>

namespace MigrationContractValidation
{
    /// <summary>保存の純粋性と、明示的な移行の保持・競合・冪等性を検証します。</summary>
    template <class Report> void Run(const std::filesystem::path& root, Report&& report)
    {
        using namespace SceneRuntime;
        std::filesystem::create_directories(root / "Assets/Scenes");
        ScriptSchema schema;
        schema.fields["speed"] = {2, 0, 10};
        schema.dataFields = {{"scene", {std::string("Assets/Scenes/Next.json")}},
                             {"nested", {ScriptValue::Object{{"new", {true}}, {"old", {1.0f}}}}},
                             {"items", {ScriptValue::Array{ScriptValue{1.0f}, ScriptValue{2.0f}}}}};
        schema.dataPurposes["scene"] = ScriptDataPurpose::SceneAsset;
        ScriptComponent script;
        script.id = "rules";
        script.behaviour = "MigrationContract";
        script.enabled = false;
        script.parameters["legacy"] = 5;
        script.data = {{"nested", {ScriptValue::Object{{"old", {9.0f}}, {"extra", {std::string("keep")}}}}},
                       {"items", {ScriptValue::Array{ScriptValue{8.0f}}}},
                       {"unknown", {ScriptObjectReference{"external"}}}};
        ScenePlacement object;
        object.id = "actor";
        object.scripts = {script};
        SceneLayout scene;
        scene.objects = {object};
        const auto before = scene.Serialize();
        Editor::SceneSnapshotCache cache;
        const auto cached = cache.Get(scene);
        ScriptDefinition binding;
        static_cast<ScriptSchema&>(binding) = schema;
        binding.update = [](ScriptContext&) {};
        std::string error;
        const auto path = root / "Assets/Scenes/Document.json";
        const auto read = [&]()
        {
            std::ifstream stream(path, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(stream), {});
        };
        scene.Save(path);
        const auto disk = read();
        const bool installed =
            ScriptRegistry::InstallModule("migration-contract", {{script.behaviour, binding}}, error);
        scene.Save(path);
        report("migration-pure-save", installed && disk == read() && scene.Serialize() == before);
        report("migration-pure-cache", cache.Get(scene) == cached && cache.Serializations() == 1);
        Editor::EditHistory history;
        history.Reset({before, "actor", {"actor"}});
        const auto result = MigrateSceneScripts(scene, {{script.behaviour, schema}});
        const auto& migrated = scene.objects.front().scripts.front();
        const auto& nested = std::get<ScriptValue::Object>(migrated.data.at("nested").value);
        report("migration-preserves-values",
               result.conflicts.empty() && !result.changes.empty() && migrated.id == script.id && !migrated.enabled &&
                   migrated.parameters.at("legacy") == 5 && migrated.parameters.at("speed") == 2 &&
                   migrated.data.at("unknown") == script.data.at("unknown") && nested.at("old") == ScriptValue{9.0f} &&
                   nested.at("extra") == ScriptValue{std::string("keep")} && nested.at("new") == ScriptValue{true} &&
                   migrated.data.at("items") == script.data.at("items") && migrated.sceneAssets.contains("scene"));
        const auto after = scene.Serialize();
        const auto repeated = MigrateSceneScripts(scene, {{script.behaviour, schema}});
        report("migration-idempotent",
               repeated.conflicts.empty() && repeated.changes.empty() && scene.Serialize() == after);
        history.Observe({after, "actor", {"actor"}}, "");
        const bool undo = history.CanUndo() && history.Target(false).json == before && history.Dirty(after);
        history.Applied(false);
        report("migration-undo-redo", undo && history.CanRedo() && history.Target(true).json == after &&
                                          cache.Get(scene) == after && cache.Serializations() == 2);
        scene.Save(path);
        const auto migratedDisk = read();
        const bool unloaded = ScriptRegistry::InstallModule("migration-contract", {}, error);
        scene.Save(path);
        report("migration-unloaded-roundtrip", unloaded && migratedDisk == read() &&
                                                   SceneLayout::Load(path).Serialize() == after &&
                                                   cache.Get(scene) == after && cache.Serializations() == 2);
        auto failed = SceneLayout::Parse(before);
        auto other = failed.objects.front();
        other.id = "second";
        other.scripts.front().data["scene"] = {true};
        failed.objects.push_back(other);
        const auto failureBefore = failed.Serialize();
        const auto conflicts = MigrateSceneScripts(failed, {{script.behaviour, schema}});
        report("migration-atomic-conflict", !conflicts.conflicts.empty() && failed.Serialize() == failureBefore &&
                                                conflicts.conflicts.front().find("second") != std::string::npos);
        auto range = script;
        range.parameters["speed"] = 20;
        const auto rangeBefore = range;
        const auto rangeResult = schema.MigrateValues(range);
        report("migration-range-conflict", !rangeResult.conflicts.empty() && range == rangeBefore);
        auto oversized = script;
        for (int i = 0; i < 64; ++i)
            oversized.parameters["extra" + std::to_string(i)] = 0;
        const auto sizeBefore = oversized;
        report("migration-limit-conflict",
               !schema.MigrateValues(oversized).conflicts.empty() && oversized == sizeBefore);
        auto unknown = SceneLayout::Parse(before);
        const auto missing = MigrateSceneScripts(unknown, {});
        report("migration-unavailable-schema", missing.conflicts.empty() && missing.changes.empty() &&
                                                   missing.unavailable.size() == 1 && unknown.Serialize() == before);
        auto textSchema = schema;
        textSchema.dataPurposes.clear();
        auto text = scene.objects.front().scripts.front();
        text.data["legacyScene"] = {std::string("Assets/Scenes/Legacy.json")};
        text.sceneAssets.insert("legacyScene");
        const auto textResult = textSchema.MigrateValues(text);
        report("migration-explicit-purpose", textResult.conflicts.empty() && !text.sceneAssets.contains("scene") &&
                                                 text.sceneAssets.contains("legacyScene"));
        auto invalid = Engine::Json::parse(before);
        invalid["objects"][0]["components"][0]["type"] = "FutureComponent";
        {
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            stream << invalid.dump();
        }
        const auto unsupported = read();
        bool rejected = false;
        try
        {
            static_cast<void>(SceneLayout::Load(path));
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        report("migration-unknown-component", rejected && read() == unsupported);
        auto legacy = Engine::Json::parse(before);
        legacy["version"] = 3;
        const auto upgraded = SceneLayout::Parse(legacy.dump()).Serialize();
        report("migration-legacy-roundtrip", Engine::Json::parse(upgraded).at("version") == 4 &&
                                                 SceneLayout::Parse(upgraded).Serialize() == upgraded);
    }
}
