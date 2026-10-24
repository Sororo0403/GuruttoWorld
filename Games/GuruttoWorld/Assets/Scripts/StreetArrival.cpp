#include <SceneRuntime/Scripts/ScriptModuleApi.h>
#include "ArrivalScriptSchema.generated.h"
#include <algorithm>

namespace
{
    using namespace SceneRuntime;

    float Smooth(float from, float to, float time)
    {
        const float progress = std::clamp((time - from) / (to - from), 0.0f, 1.0f);
        return progress * progress * (3.0f - 2.0f * progress);
    }

    void Reply(ScriptContext& context, const char* reference, const char* flag, float time)
    {
        if (context.state["elapsed"] < time || context.state[flag] != 0)
            return;
        context.state[flag] = 1;
        if (const auto* sound = context.Reference(reference))
            context.scene->PlayAudio(sound->id);
    }

    ScriptDefinition Arrival()
    {
        auto definition = GeneratedSchemas::StreetArrivalSchema();
        definition.start = [](ScriptContext& context)
        {
            context.state["elapsed"] = 0;
            context.state["nearPlayed"] = 0;
            context.state["farPlayed"] = 0;
            context.state["completed"] = 0;
            if (context.scene)
                context.scene->SetUiValue("arrivalMusicGain", 0.18f);
        };
        definition.update = [](ScriptContext& context)
        {
            if (!context.scene)
                return;
            if (context.state["completed"] != 0)
                return;
            context.state["elapsed"] += static_cast<float>(context.seconds);
            const float elapsed = context.state["elapsed"];
            const float release = context.Value("release", 0.92f);
            // 溜めの直前を静かにし、街から返る音の後でBGMを元の音量へ戻します。
            const float hush = Smooth(release - 0.12f, release - 0.04f, elapsed);
            const float returnToStreet = Smooth(release + 0.12f, 1.80f, elapsed);
            const float gain = (0.18f - 0.12f * hush) * (1.0f - returnToStreet) + returnToStreet;
            context.scene->SetUiValue("arrivalMusicGain", gain);
            Reply(context, "nearSound", "nearPlayed", context.Value("nearTime", 0.991053f));
            Reply(context, "farSound", "farPlayed", context.Value("farTime", 1.024211f));
            // 演出の完了後はゲームを継続し、以降の音量操作を妨げません。
            if (elapsed >= 2.05f)
            {
                context.state["completed"] = 1;
            }
        };
        return definition;
    }
}

bool Register_StreetArrival(void* owner, SceneRuntime::RegisterModuleScript add)
{
    const auto definition = Arrival();
    return add(owner, "StreetArrival", &definition);
}
