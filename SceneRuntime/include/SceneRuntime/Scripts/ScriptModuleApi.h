#pragma once
#include <SceneRuntime/Scripts/RuntimeScripts.h>
#include <cstdint>
#include <Engine/Core/BuildIdentity.h>

namespace SceneRuntime
{
    inline constexpr uint32_t ScriptModuleAbiVersion = 18;
    struct ScriptModuleAbi
    {
        uint32_t version = ScriptModuleAbiVersion;
        uint64_t engineBuild = Engine::BuildIdentity();
        uint32_t compiler = _MSC_VER;
        uint32_t iteratorDebug = _ITERATOR_DEBUG_LEVEL;
        uint32_t contextBytes = sizeof(RuntimeScriptContext);
        uint32_t componentBytes = sizeof(ScenePlacement);
        uint32_t callbacksBytes = sizeof(RuntimeScriptDefinition);
        uint32_t sceneBytes = sizeof(RuntimeScriptScene);
        uint32_t entityBytes = sizeof(EntityHandle);
        uint32_t instanceBytes = sizeof(ComponentInstanceHandle);
        uint32_t stopBytes = sizeof(RuntimeScriptStopContext);
        bool operator==(const ScriptModuleAbi&) const = default;
    };
    using RegisterModuleScript = bool (*)(void*, const char*, const RuntimeScriptDefinition*);
    using RegisterScriptModule = bool (*)(const ScriptModuleAbi*, void*, RegisterModuleScript);
}
