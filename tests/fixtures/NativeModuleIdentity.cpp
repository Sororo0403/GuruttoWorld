#include <SceneRuntime/Scripts/ScriptModuleApi.h>

namespace SceneRuntime
{
    class ScriptRuntime;
    class PhysicsWorld;
}

template <class Type>
concept CompleteType = requires { sizeof(Type); };

// DLLの公開入口が、ホストの実行状態や物理実装の定義を取り込まないことを検査します。
static_assert(!CompleteType<SceneRuntime::ScriptRuntime>);
static_assert(!CompleteType<SceneRuntime::PhysicsWorld>);

bool Register_IdentityProbe(void* context, SceneRuntime::RegisterModuleScript add)
{
    SceneRuntime::RuntimeScriptDefinition definition;
    definition.update = [](SceneRuntime::RuntimeScriptContext&) {};
    return add(context, "IdentityProbe", &definition);
}
