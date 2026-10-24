#pragma once
#include <SceneRuntime/Scene/SceneLayout.h>
#include <SceneRuntime/Scripts/ScriptSchema.h>

namespace SceneRuntime
{
    /// <summary>シーン全体へ明示的にスキーマを適用します。競合時は元のシーンを保持します。</summary>
    ScriptMigrationReport MigrateSceneScripts(SceneLayout& layout, const std::map<std::string, ScriptSchema>& schemas);
}
