#pragma once
#include <SceneRuntime/ScriptRuntime.h>
#include <cstdint>

namespace SceneRuntime
{
    struct ScriptModuleAbi
    {
        uint32_t version=1;
        uint32_t compiler=_MSC_VER;
        uint32_t iteratorDebug=_ITERATOR_DEBUG_LEVEL;
        uint32_t contextBytes=sizeof(ScriptContext);
        uint32_t componentBytes=sizeof(ScenePlacement);
        uint32_t definitionBytes=sizeof(ScriptDefinition);
        bool operator==(const ScriptModuleAbi&) const = default;
    };
    using RegisterModuleScript=bool (*)(void*,const char*,const ScriptDefinition*);
    using RegisterScriptModule=bool (*)(const ScriptModuleAbi*,void*,RegisterModuleScript);
}
