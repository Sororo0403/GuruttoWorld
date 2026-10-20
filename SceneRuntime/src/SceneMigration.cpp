#include <SceneRuntime/SceneMigration.h>

namespace SceneRuntime
{
    ScriptMigrationReport MigrateSceneScripts(SceneLayout& layout, const std::map<std::string, ScriptSchema>& schemas)
    {
        auto candidate = layout;
        ScriptMigrationReport report;
        for (auto& object : candidate.objects)
        {
            for (auto& script : object.scripts)
            {
                const auto location = "Object " + object.id + ", Script " + script.id + ": ";
                const auto schema = schemas.find(script.behaviour);
                if (schema == schemas.end())
                {
                    report.unavailable.push_back(location + script.behaviour);
                    continue;
                }
                const auto result = schema->second.MigrateValues(script);
                for (const auto& change : result.changes)
                    report.changes.push_back(location + change);
                for (const auto& conflict : result.conflicts)
                    report.conflicts.push_back(location + conflict);
            }
        }
        if (report.conflicts.empty())
        {
            try
            {
                candidate.Validate();
                layout = std::move(candidate);
            }
            catch (const std::exception& error)
            {
                report.conflicts.push_back(error.what());
            }
        }
        // Prefabのbaselineは過去の比較基準なので、現在のスキーマで書き換えません。
        return report;
    }
}
