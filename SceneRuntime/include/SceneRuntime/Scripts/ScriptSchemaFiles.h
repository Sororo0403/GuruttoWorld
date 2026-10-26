#pragma once
#include <SceneRuntime/Scripts/ScriptSchema.h>
#include <fstream>

namespace SceneRuntime
{
    class ScriptSchemaFiles final
    {
    public:
        /// <summary>宣言ファイルを取得してからデータ層で形式と継承を検証します。</summary>
        static std::map<std::string, ScriptSchema> LoadProject(const std::filesystem::path& root);
    };
}
