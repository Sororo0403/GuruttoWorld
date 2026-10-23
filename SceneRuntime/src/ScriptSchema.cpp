#include <SceneRuntime/ScriptSchema.h>
#include <SceneRuntime/SceneComponents.h>
#include <SceneRuntime/ScriptValueJson.h>
#include "SceneComponentJson.h"
#include <Engine/Core/Validation.h>
#include <fstream>
#include <functional>
#include <set>

namespace SceneRuntime
{
    namespace
    {
        int ValidateSchemaDocument(const Engine::Json& source)
        {
            const auto& version = source.at("version");
            if (!version.is_number_integer() || (version != 1 && version != 2))
                throw std::runtime_error("Unsupported script schema version");
            const auto& scripts = Engine::JsonObject(source.at("scripts"));
            if (scripts.size() > 256)
                throw std::runtime_error("Too many script schemas");
            for (const auto& [name, definition] : scripts.items())
            {
                static_cast<void>(name);
                Engine::JsonObject(definition);
                if (version == 1 && definition.contains("extends"))
                    throw std::runtime_error("Schema inheritance requires version 2");
                if (version == 1)
                    for (const auto* section : {"parameters", "data", "purposes"})
                    {
                        if (!definition.contains(section))
                            continue;
                        for (const auto& [key, value] : Engine::JsonObject(definition.at(section)).items())
                        {
                            static_cast<void>(key);
                            if (value.is_null())
                                throw std::runtime_error("Schema deletions require version 2");
                        }
                    }
            }
            return version.get<int>();
        }
        Engine::Json FlattenSchemas(const Engine::Json& source)
        {
            const auto version = ValidateSchemaDocument(source);
            const auto& declarations = source.at("scripts");
            auto resolved = Engine::Json::object();
            std::set<std::string> active;
            std::map<std::string, size_t> heights;
            std::function<Engine::Json(const std::string&, size_t)> resolve;
            resolve = [&](const std::string& name, size_t depth) -> Engine::Json
            {
                if (depth > 16 || !declarations.contains(name))
                    throw std::runtime_error("Missing or excessively nested base schema: " + name);
                if (resolved.contains(name))
                    return resolved.at(name);
                if (!active.insert(name).second)
                    throw std::runtime_error("Cyclic script schema inheritance: " + name);
                const auto& declaration = declarations.at(name);
                size_t height = 0;
                auto schema = Engine::Json{{"parameters", Engine::Json::object()},
                                           {"data", Engine::Json::object()},
                                           {"purposes", Engine::Json::object()}};
                if (declaration.contains("extends"))
                {
                    const auto base = declaration.at("extends").get<std::string>();
                    if (base.empty())
                        throw std::runtime_error("Empty base script schema");
                    schema = resolve(base, depth + 1);
                    height = heights.at(base) + 1;
                    if (height > 16)
                        throw std::runtime_error("Script schema inheritance exceeds sixteen");
                }
                for (const auto* section : {"parameters", "data", "purposes"})
                {
                    if (!declaration.contains(section))
                        continue;
                    for (const auto& [key, value] : Engine::JsonObject(declaration.at(section)).items())
                        if (version == 2 && value.is_null())
                            schema[section].erase(key);
                        else
                            schema[section][key] = value;
                }
                active.erase(name);
                heights[name] = height;
                resolved[name] = schema;
                return schema;
            };
            for (const auto& [name, declaration] : declarations.items())
            {
                static_cast<void>(declaration);
                static_cast<void>(resolve(name, 0));
            }
            return resolved;
        }
        std::string FieldPath(const std::string& parent, const std::string& key)
        {
            std::string result = parent + "/";
            for (const char c : key)
                result += c == '~' ? "~0" : c == '/' ? "~1" : std::string(1, c);
            return result;
        }
        void MergeDefaults(ScriptValue::Object& values, const ScriptValue::Object& defaults, const std::string& path,
                           ScriptMigrationReport& report)
        {
            for (const auto& [key, initial] : defaults)
            {
                const auto childPath = FieldPath(path, key);
                const auto found = values.find(key);
                if (found == values.end())
                {
                    values.emplace(key, initial);
                    report.changes.push_back(childPath);
                }
                else if (found->second.value.index() != initial.value.index())
                    report.conflicts.push_back(childPath + ": type differs from schema");
                else if (const auto object = std::get_if<ScriptValue::Object>(&initial.value))
                    MergeDefaults(std::get<ScriptValue::Object>(found->second.value), *object, childPath, report);
                // 配列は利用者の順序と要素数を保持し、既定値の要素を推測で足しません。
            }
        }
    }
    ScriptMigrationReport ScriptSchema::MigrateValues(ScriptComponent& component) const
    {
        ScriptMigrationReport report;
        auto candidate = component;
        try
        {
            Validate(component.behaviour);
            for (const auto& [key, field] : fields)
            {
                const auto found = candidate.parameters.find(key);
                if (found == candidate.parameters.end())
                {
                    candidate.parameters.emplace(key, field.initial);
                    report.changes.push_back(FieldPath("/parameters", key));
                }
                else if (!Engine::InRange(found->second, field.minimum, field.maximum))
                    report.conflicts.push_back(FieldPath("/parameters", key) + ": outside schema range");
            }
            MergeDefaults(candidate.data, dataFields, "/data", report);
            for (const auto& [key, initial] : dataFields)
            {
                static_cast<void>(initial);
                const auto purpose = dataPurposes.find(key);
                const bool asset = purpose != dataPurposes.end() && purpose->second == ScriptDataPurpose::SceneAsset;
                const bool wasAsset = candidate.sceneAssets.contains(key);
                if (asset)
                    candidate.sceneAssets.insert(key);
                else
                    candidate.sceneAssets.erase(key);
                if (asset != wasAsset)
                    report.changes.push_back(FieldPath("/sceneAssets", key));
            }
            // 保存用の同じcodecで総数・深さ・参照指定も検証し、移行だけが不正値を作らないようにします。
            ScenePlacement checked;
            ReadSceneComponents(Engine::Json{{"components", Engine::Json::array({WriteSceneComponent(candidate)})}},
                                checked, false);
        }
        catch (const std::exception& error)
        {
            report.conflicts.push_back(error.what());
        }
        if (report.conflicts.empty())
            component = std::move(candidate);
        return report;
    }
    static void ValidateParameterFields(const std::map<std::string, ScriptField>& fields)
    {
        for (const auto& [key, field] : fields)
        {
            if (key.empty() || key.size() > 128 || key.find('\0') != std::string::npos ||
                !Engine::InRange(field.initial, field.minimum, field.maximum) ||
                !Engine::InRange(field.minimum, -1000000, 1000000) ||
                !Engine::InRange(field.maximum, -1000000, 1000000))
            {
                throw std::runtime_error("Invalid script parameter schema");
            }
        }
    }
    void ScriptSchema::Validate(const std::string& name) const
    {
        if (name.empty() || name.size() > 128 || name.find('\0') != std::string::npos || fields.size() > 64)
        {
            throw std::runtime_error("Invalid script schema name or size");
        }
        ValidateParameterFields(fields);
        size_t remaining = 4096;
        ScriptValue{dataFields}.Validate(0, remaining);
        for (const auto& [key, purpose] : dataPurposes)
        {
            const auto field = dataFields.find(key);
            if (field == dataFields.end() || !std::holds_alternative<std::string>(field->second.value) ||
                (purpose != ScriptDataPurpose::Text && purpose != ScriptDataPurpose::SceneAsset))
            {
                throw std::runtime_error("Invalid script data purpose");
            }
        }
    }
    void ScriptSchema::ResetValues(ScriptComponent& component) const
    {
        Validate(component.behaviour);
        component.enabled = true;
        component.parameters.clear();
        for (const auto& [name, field] : fields)
        {
            component.parameters[name] = field.initial;
        }
        component.data = dataFields;
        component.sceneAssets.clear();
        for (const auto& [name, purpose] : dataPurposes)
        {
            if (purpose == ScriptDataPurpose::SceneAsset)
            {
                component.sceneAssets.insert(name);
            }
        }
    }
    Engine::Json ScriptSchema::Serialize() const
    {
        Validate("Schema");
        auto value = Engine::Json{{"parameters", Engine::Json::object()},
                                  {"data", Engine::Json::object()},
                                  {"purposes", Engine::Json::object()}};
        for (const auto& [name, field] : fields)
            value["parameters"][name] = {
                {"initial", field.initial}, {"minimum", field.minimum}, {"maximum", field.maximum}};
        for (const auto& [name, data] : dataFields)
            value["data"][name] = WriteScriptValue(data);
        for (const auto& [name, purpose] : dataPurposes)
            value["purposes"][name] = purpose == ScriptDataPurpose::SceneAsset ? "sceneAsset" : "text";
        return value;
    }
    static void ValidateSchemaIdentifier(const std::string& name)
    {
        const auto identifier = [](unsigned char c)
        {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
        };
        if (name.empty() || name.size() > 64 || !identifier(static_cast<unsigned char>(name.front())) ||
            !std::all_of(name.begin(), name.end(),
                         [&](unsigned char c)
                         {
                             return identifier(c) || (c >= '0' && c <= '9');
                         }))
        {
            throw std::runtime_error("Script schema name must be a C++ identifier");
        }
    }
    std::map<std::string, ScriptSchema> ScriptSchema::Parse(const Engine::Json& source)
    {
        const auto declarations = FlattenSchemas(source);
        std::map<std::string, ScriptSchema> result;
        for (const auto& [name, definition] : declarations.items())
        {
            ValidateSchemaIdentifier(name);
            ScriptSchema schema;
            const auto parameters = definition.value("parameters", Engine::Json::object());
            const auto data = definition.value("data", Engine::Json::object());
            const auto purposes = definition.value("purposes", Engine::Json::object());
            for (const auto& [key, field] : Engine::JsonObject(parameters).items())
            {
                schema.fields.emplace(key, ScriptField{static_cast<float>(Engine::JsonNumber(field.at("initial"))),
                                                       static_cast<float>(Engine::JsonNumber(field.at("minimum"))),
                                                       static_cast<float>(Engine::JsonNumber(field.at("maximum")))});
            }
            size_t remaining = 4095;
            for (const auto& [key, value] : Engine::JsonObject(data).items())
            {
                schema.dataFields.emplace(key, ReadScriptValue(value, 1, remaining));
            }
            for (const auto& [key, purpose] : Engine::JsonObject(purposes).items())
            {
                if (purpose != "sceneAsset" && purpose != "text")
                {
                    throw std::runtime_error("Unsupported script schema purpose");
                }
                schema.dataPurposes.emplace(key, purpose == "sceneAsset" ? ScriptDataPurpose::SceneAsset
                                                                         : ScriptDataPurpose::Text);
            }
            schema.Validate(name);
            result.emplace(name, std::move(schema));
        }
        return result;
    }
    std::map<std::string, ScriptSchema> ScriptSchema::LoadProject(const std::filesystem::path& root)
    {
        auto declarations = Engine::Json::object();
        const auto source = root / "Assets/Scripts";
        if (!std::filesystem::is_directory(source))
            return {};
        for (const auto& entry : std::filesystem::recursive_directory_iterator(source))
        {
            if (!entry.is_regular_file() || !entry.path().filename().generic_string().ends_with(".schema.json"))
                continue;
            std::ifstream file(entry.path());
            if (!file)
                throw std::runtime_error("Cannot read script schema");
            const auto document = Engine::Json::parse(file);
            static_cast<void>(ValidateSchemaDocument(document));
            for (const auto& [name, declaration] : document.at("scripts").items())
            {
                if (declarations.contains(name) || declarations.size() >= 256)
                    throw std::runtime_error("Duplicate script schema or too many behaviours");
                declarations[name] = declaration;
            }
        }
        return Parse(Engine::Json{{"version", 2}, {"scripts", declarations}});
    }
}
