#pragma once
#include <SceneRuntime/Scripts/ScriptSchema.h>
#include <SceneRuntime/Scene/ComponentFieldSchema.h>
#include <SceneRuntime/Scripts/ScriptValueJson.h>
#include "../../../Editor/src/Project/ProjectCatalog.h"
#include <fstream>
#include <limits>

namespace SchemaContractValidation
{
    /// <summary>DLLなしのスキーマ読込と、登録・保存・編集が共有する制約を検証します。</summary>
    template <class Report> void Run(const std::filesystem::path& root, Report&& report)
    {
        using namespace SceneRuntime;
        const auto schemas = ScriptSchema::LoadProject("Games/GuruttoWorld");
        report("schema-existing-format",
               schemas.contains("ParkPlayer") && schemas.contains("ParkCamera") && schemas.contains("ParkPatrol"));
        const auto scene = std::string("Assets/Scenes/Declared.json");
        const auto definition =
            Engine::Json{{"parameters", {{"speed", {{"initial", 2}, {"minimum", 0}, {"maximum", 10}}}}},
                         {"data",
                          {{"scene", {{"kind", "string"}, {"value", scene}}},
                           {"text", {{"kind", "string"}, {"value", scene}}},
                           {"target", {{"kind", "reference"}, {"value", "target"}}}}},
                         {"purposes", {{"scene", "sceneAsset"}}}};
        auto document = Engine::Json{{"version", 1}, {"scripts", {{"DeclaredContract", definition}}}};
        std::filesystem::create_directories(root / "Assets/Scripts");
        const auto path = root / "Assets/Scripts/Declared.schema.json";
        const auto save = [](const auto& path, const auto& value)
        {
            std::ofstream output(path);
            output << value.dump(2);
            output.close();
            if (!output)
            {
                throw std::runtime_error("Cannot write schema fixture");
            }
        };
        save(path, document);
        Editor::ProjectCatalog catalog;
        const bool scanned = catalog.Scan(root);
        report("schema-independent-catalog", scanned && catalog.ScriptSchemas().contains("DeclaredContract") &&
                                                 !ScriptRegistry::Definitions().contains("DeclaredContract"));
        const auto schema = catalog.ScriptSchemas().at("DeclaredContract");
        ScriptComponent component;
        component.behaviour = "DeclaredContract";
        component.data["extra"] = {3.0f};
        schema.ResetValues(component);
        report("schema-explicit-defaults", component.parameters.at("speed") == 2 &&
                                               component.data == schema.dataFields &&
                                               component.sceneAssets == std::set<std::string>{"scene"});
        size_t remaining = 4096;
        const ScriptValue data{schema.dataFields};
        report("schema-value-codec", ReadScriptValue(WriteScriptValue(data), 0, remaining) == data);
        ScriptDefinition binding;
        static_cast<ScriptSchema&>(binding) = schema;
        binding.update = [](ScriptContext&) {};
        report("schema-runtime-validation", ScriptRegistry::Register("DeclaredRuntimeContract", binding));
        binding.fields.at("speed").initial = std::numeric_limits<float>::infinity();
        report("schema-invalid-runtime", !ScriptRegistry::Register("InvalidRuntimeContract", binding));
        document["scripts"]["DeclaredContract"]["parameters"]["speed"]["initial"] = 20;
        save(path, document);
        report("schema-failed-refresh",
               !catalog.Scan(root) && catalog.ScriptSchemas().at("DeclaredContract") == schema);
        document["scripts"]["DeclaredContract"] = definition;
        save(path, document);
        save(root / "Assets/Scripts/Duplicate.schema.json", document);
        bool duplicate = false;
        try
        {
            static_cast<void>(ScriptSchema::LoadProject(root));
        }
        catch (const std::exception&)
        {
            duplicate = true;
        }
        report("schema-duplicate-rejected", duplicate);
        bool limits = true;
        for (const auto& field : PlayerControllerScalarFields)
        {
            SceneLayout layout;
            ScenePlacement owner;
            owner.id = "player";
            owner.playerController.emplace();
            owner.playerController.value().*field.member = field.maximum;
            layout.objects.push_back(owner);
            const auto encoded = Engine::Json::parse(layout.Serialize());
            limits =
                limits && SceneLayout::Parse(encoded.dump()).objects.front().playerController.value().*field.member ==
                              field.maximum;
            auto invalid = encoded;
            invalid["objects"][0]["components"][0][std::string(field.name)] = field.maximum + 1;
            bool rejected = false;
            try
            {
                static_cast<void>(SceneLayout::Parse(invalid.dump()));
            }
            catch (const std::exception&)
            {
                rejected = true;
            }
            layout.objects.front().playerController.value().*field.member = field.minimum - 1;
            bool saveRejected = false;
            try
            {
                static_cast<void>(layout.Serialize());
            }
            catch (const std::exception&)
            {
                saveRejected = true;
            }
            limits = limits && rejected && saveRejected && &PlayerControllerField(field.member) == &field;
        }
        report("schema-player-limits", limits);
    }
}
