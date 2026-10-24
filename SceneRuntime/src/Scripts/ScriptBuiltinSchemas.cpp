#include <SceneRuntime/Scripts/ScriptSchema.h>

namespace SceneRuntime
{
    const std::map<std::string, ScriptSchema>& ScriptSchema::Builtins()
    {
        static const auto schemas = []
        {
            std::map<std::string, ScriptSchema> result;
            result["Bob"].fields = {{"amplitude", {0.5f, 0, 1000}}, {"frequency", {1, 0, 1000}}};
            result["Spin"].fields = {{"speed", {90, -100000, 100000}}};
            result["AnimationEventPulse"].fields = {{"duration", {.2f, .01f, 60}}, {"peak", {15, 0, 10000}}};
            result["IkOrbit"].fields = {
                {"radius", {.25f, 0, 1000}}, {"frequency", {.3f, 0, 1000}}, {"weight", {1, 0, 1}}};
            result["RootMotionControl"].fields = {{"enabled", {1, -1, 1}}};
            result["FollowTarget"].dataFields = {
                {"active", {true}},
                {"target", {ScriptObjectReference{}}},
                {"offset", {ScriptValue::Object{{"x", {0.0f}}, {"y", {0.0f}}, {"z", {0.0f}}}}}};
            return result;
        }();
        return schemas;
    }
}
