#pragma once
#include <SceneRuntime/SceneLayout.h>
#include <functional>
#include <map>

namespace SceneRuntime
{
    struct ScriptField { float initial=0,minimum=-1000000,maximum=1000000; };
    struct ScriptContext
    {
        ScenePlacement& object;
        const std::map<std::string,float>& parameters;
        std::map<std::string,float>& state;
        double seconds=0;
        float Value(const std::string& name,float fallback=0) const;
    };
    struct ScriptDefinition
    {
        std::map<std::string,ScriptField> fields;
        std::function<void(ScriptContext&)> start,update,stop;
    };
    class ScriptRegistry final
    {
    public:
        // Register before Play, on the main thread. Duplicate names and invalid metadata are refused.
        static bool Register(std::string name,ScriptDefinition definition);
        static const std::map<std::string,ScriptDefinition>& Definitions();
    };
    class ScriptRuntime final
    {
    public:
        // Update only changes transforms; callbacks may keep numeric per-instance state.
        bool Update(SceneLayout& layout,double seconds,std::string& error);
        void Stop(SceneLayout& layout) noexcept;
    private:
        struct Instance { std::string owner,id,behaviour; std::map<std::string,float> state,parameters; };
        std::map<std::pair<std::string,std::string>,Instance> instances_;
    };
}
