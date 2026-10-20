#include <SceneRuntime/ScriptModuleApi.h>

bool Register_IdentityProbe(void* context, SceneRuntime::RegisterModuleScript add)
{
    SceneRuntime::ScriptDefinition definition;
    definition.update = [](SceneRuntime::ScriptContext&) {};
    return add(context, "IdentityProbe", &definition);
}
