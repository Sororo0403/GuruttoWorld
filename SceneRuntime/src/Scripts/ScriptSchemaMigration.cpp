#include <SceneRuntime/Scripts/ScriptSchema.h>
#include <SceneRuntime/Scene/SceneComponents.h>
#include "../Scene/SceneComponentJson.h"
#include <Engine/Core/Validation.h>

namespace SceneRuntime
{
    namespace
    {
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
            // 保存用の同じシリアライズ処理で総数・深さ・参照指定も検証し、移行だけが不正値を作らないようにします。
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
}
