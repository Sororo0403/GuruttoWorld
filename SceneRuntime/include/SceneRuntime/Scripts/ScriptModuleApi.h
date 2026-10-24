#pragma once
#include <SceneRuntime/Scripts/ScriptApi.h>
#include <cstdint>
#include <Engine/Core/BuildIdentity.h>

namespace SceneRuntime
{
    struct ScriptModuleAbi
    {
        uint32_t version = 14;
        uint64_t engineBuild=Engine::BuildIdentity();
        uint32_t compiler=_MSC_VER;
        uint32_t iteratorDebug=_ITERATOR_DEBUG_LEVEL;
        uint32_t contextBytes=sizeof(ScriptContext);
        uint32_t componentBytes=sizeof(ScenePlacement);
        uint32_t definitionBytes=sizeof(ScriptDefinition);
        uint32_t sceneBytes=sizeof(ScriptScene);
        bool operator==(const ScriptModuleAbi&) const = default;
    };
    using RegisterModuleScript=bool (*)(void*,const char*,const ScriptDefinition*);
    using RegisterScriptModule=bool (*)(const ScriptModuleAbi*,void*,RegisterModuleScript);
}
