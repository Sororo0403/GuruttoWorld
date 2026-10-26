#pragma once
#include <LegacyScripts/ScriptRuntime.h>
#include <SceneRuntime/Scripts/ScriptModule.h>
#include <Engine/Core/FileContents.h>
#include "../../../Editor/src/Project/ProjectCatalog.h"

namespace ExtensibilityContractValidation
{
    /// <summary>JSON宣言の継承・独立性を検証し、実行処理への暗黙の依存を防ぎます。</summary>
    template <class Contract> void Run(const std::filesystem::path& root, Contract contract)
    {
        using namespace SceneRuntime;
        using Engine::Json;
        const Json base{{"parameters", {{"speed", {{"initial", 2}, {"minimum", 0}, {"maximum", 10}}}}},
                        {"data",
                         {{"next", {{"kind", "string"}, {"value", "Assets/Scenes/Next.json"}}},
                          {"target", {{"kind", "reference"}, {"value", ""}}}}},
                        {"purposes", {{"next", "sceneAsset"}}}};
        auto document = Json{
            {"version", 2},
            {"scripts",
             {{"Base", base},
              {"Derived",
               {{"extends", "Base"}, {"parameters", {{"speed", {{"initial", 4}, {"minimum", 1}, {"maximum", 8}}}}}}}}}};
        const auto parsed = ScriptSchema::Parse(document);
        contract("schema-inheritance-reuses-declaration",
                 parsed.at("Base").fields.at("speed").initial == 2 &&
                     parsed.at("Derived").fields.at("speed").initial == 4 &&
                     parsed.at("Derived").dataFields == parsed.at("Base").dataFields &&
                     parsed.at("Derived").dataPurposes == parsed.at("Base").dataPurposes);
        document["scripts"]["Derived"]["data"]["target"] = nullptr;
        document["scripts"]["Derived"]["purposes"]["next"] = "text";
        auto removed = ScriptSchema::Parse(document);
        contract("schema-inheritance-explicit-removal",
                 !removed.at("Derived").dataFields.contains("target") &&
                     removed.at("Derived").dataPurposes.at("next") == ScriptDataPurpose::Text &&
                     parsed.at("Base").dataFields.contains("target"));
        contract(
            "schema-declaration-roundtrip",
            ScriptSchema::Parse(Json{{"version", 1}, {"scripts", {{"Roundtrip", removed.at("Derived").Serialize()}}}})
                    .at("Roundtrip") == removed.at("Derived"));
        const auto rejects = [](const Json& source)
        {
            try
            {
                static_cast<void>(ScriptSchema::Parse(source));
                return false;
            }
            catch (const std::exception&)
            {
                return true;
            }
        };
        auto bad = document;
        bad["version"] = 1;
        contract("schema-inheritance-version-boundary", rejects(bad));
        bad = document;
        bad["scripts"]["Base"]["extends"] = "Derived";
        contract("schema-inheritance-cycle", rejects(bad));
        bad = document;
        bad["scripts"]["Derived"]["extends"] = "Missing";
        contract("schema-inheritance-missing-base", rejects(bad));
        auto deep = Json{{"version", 2}, {"scripts", Json::object()}};
        deep["scripts"]["Depth0"] = base;
        for (int i = 1; i <= 17; ++i)
            deep["scripts"]["Depth" + std::to_string(i)] = {{"extends", "Depth" + std::to_string(i - 1)}};
        contract("schema-inheritance-depth-independent-of-cache", rejects(deep));
        bad = document;
        bad["scripts"]["Derived"]["data"]["next"] = {{"kind", "number"}, {"value", 3}};
        contract("schema-inheritance-purpose-type-conflict", rejects(bad));

        std::filesystem::create_directories(root / "Assets/Scripts");
        Engine::FileContents::Write(root / "Assets/Scripts/Base.schema.json",
                                    Json{{"version", 1}, {"scripts", {{"Base", base}}}}.dump());
        Engine::FileContents::Write(root / "Assets/Scripts/Derived.schema.json",
                                    Json{{"version", 2}, {"scripts", {{"Derived", {{"extends", "Base"}}}}}}.dump());
        const auto project = ScriptSchema::LoadProject(root);
        contract("schema-inheritance-across-files", project.at("Derived") == project.at("Base"));
        ScriptDefinition legacy;
        legacy.fields["runtimeOnly"] = {1, 0, 2};
        legacy.update = [](ScriptContext&) {};
        ScriptRegistry::Register("UndeclaredRuntimeOnly", legacy);
        Editor::ProjectCatalog catalog;
        contract("schema-catalog-excludes-runtime-fallback",
                 catalog.Scan(root) && catalog.ScriptSchemas().contains("Derived") &&
                     catalog.ScriptSchemas().contains("Spin") &&
                     !catalog.ScriptSchemas().contains("UndeclaredRuntimeOnly"));
        const auto builtins = ScriptSchema::Builtins();
        ScriptRegistry::Register("AnotherRuntimeOnly", legacy);
        contract("schema-builtin-declarations-stable",
                 ScriptSchema::Builtins() == builtins && !builtins.contains("AnotherRuntimeOnly"));
        std::filesystem::path created;
        std::string error;
        contract("schema-source-template-single-authority",
                 ScriptModule::CreateSource(root, "DeclaredMover", created, error) &&
                     Engine::FileContents::Read(created)->find("SceneRuntime::RuntimeScriptDefinition script") !=
                         std::string::npos &&
                     Engine::FileContents::Read(created)->find(".generated.h") == std::string::npos &&
                     Engine::FileContents::Read(created)->find("dataFields") == std::string::npos &&
                     ScriptSchema::LoadProject(root).at("DeclaredMover").fields.at("speed").initial == 2);
        const auto beforeSource = Engine::FileContents::Read(created);
        const auto beforeSchema = Engine::FileContents::Read(root / "Assets/Scripts/DeclaredMover.schema.json");
        contract("schema-source-template-preserves-existing",
                 !ScriptModule::CreateSource(root, "DeclaredMover", created, error) &&
                     Engine::FileContents::Read(root / "Assets/Scripts/DeclaredMover.cpp") == beforeSource &&
                     Engine::FileContents::Read(root / "Assets/Scripts/DeclaredMover.schema.json") == beforeSchema);
        Engine::FileContents::Write(root / "Assets/Scripts/Reserved.schema.json", "existing");
        contract("schema-source-template-declaration-collision",
                 !ScriptModule::CreateSource(root, "Reserved", created, error) &&
                     !Engine::FileContents::Read(root / "Assets/Scripts/Reserved.cpp") &&
                     Engine::FileContents::Read(root / "Assets/Scripts/Reserved.schema.json") == "existing");
    }
}
