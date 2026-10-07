#include <SceneRuntime/ScriptRuntime.h>
#include <Engine/Core/Log.h>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace
{
    using namespace SceneRuntime;
    std::map<std::string,ScriptDefinition>& Registry()
    {
        static auto definitions=[] {
            std::map<std::string,ScriptDefinition> result;
            ScriptDefinition bob; bob.fields={{"amplitude",{0.5f,0,1000}},{"frequency",{1,0,1000}}};
            bob.start=[](ScriptContext& context) { context.state["origin"]=context.object.position[1]; context.state["time"]=0; };
            bob.update=[](ScriptContext& context) {
                context.state["time"]+=static_cast<float>(context.seconds);
                context.object.position[1]=context.state["origin"]+std::sin(context.state["time"]*context.Value("frequency",1)*6.2831853f)*context.Value("amplitude",0.5f);
            };
            result.emplace("Bob",std::move(bob));
            ScriptDefinition spin; spin.fields={{"speed",{90,-100000,100000}}};
            spin.update=[](ScriptContext& context) { context.object.rotation[1]=std::remainder(context.object.rotation[1]+static_cast<float>(context.seconds)*context.Value("speed",90)*0.0174532925f,6.2831853f); };
            result.emplace("Spin",std::move(spin));
            return result;
        }();
        return definitions;
    }
    void Validate(const ScriptComponent& script,const ScriptDefinition& definition)
    {
        for (const auto& [name,field] : definition.fields)
        {
            const auto found=script.parameters.find(name);
            const float value=found==script.parameters.end() ? field.initial : found->second;
            if (!std::isfinite(value) || value<field.minimum || value>field.maximum) throw std::runtime_error("Script parameter outside range: "+name);
        }
    }
}
namespace SceneRuntime
{
    float ScriptContext::Value(const std::string& name,float fallback) const
    { const auto found=parameters.find(name); return found==parameters.end() ? fallback : found->second; }
    bool ScriptRegistry::Register(std::string name,ScriptDefinition definition)
    {
        if (name.empty() || name.size()>128 || name.find('\0')!=std::string::npos || definition.fields.size()>64 || !definition.update) return false;
        for (const auto& [key,field] : definition.fields)
            if (key.empty() || key.size()>128 || !std::isfinite(field.initial) || !std::isfinite(field.minimum) || !std::isfinite(field.maximum) ||
                field.initial<field.minimum || field.initial>field.maximum || field.minimum<-1000000 || field.maximum>1000000) return false;
        return Registry().emplace(std::move(name),std::move(definition)).second;
    }
    const std::map<std::string,ScriptDefinition>& ScriptRegistry::Definitions() { return Registry(); }
    float ScriptContext::Input(const std::string& name) const
    { if (!input) return 0; const auto found=input->find(name); return found==input->end() ? 0 : found->second; }
    bool ScriptContext::Pressed(const std::string& name) const
    { if (!pressed) return false; const auto found=pressed->find(name); return found!=pressed->end() && found->second; }
    bool ScriptRuntime::Update(SceneLayout& layout,double seconds,std::string& error,const std::map<std::string,float>& input,const std::map<std::string,bool>& pressed)
    {
        if (!std::isfinite(seconds) || seconds<=0) { error="Invalid script time"; return false; }
        try
        {
            std::set<std::pair<std::string,std::string>> live;
            for (auto& object : layout.objects)
                for (const auto& script : object.scripts)
                {
                    if (!script.enabled) continue;
                    const auto definition=Registry().find(script.behaviour);
                    if (definition==Registry().end()) throw std::runtime_error("Script behaviour is not registered: "+script.behaviour);
                    Validate(script,definition->second);
                    const auto key=std::pair{object.id,script.id}; live.insert(key);
                    auto found=instances_.find(key);
                    if (found!=instances_.end() && found->second.behaviour!=script.behaviour)
                    {
                        auto& old=found->second;
                        ScriptContext context{object,old.parameters,old.state,0};
                        const auto& previous=Registry().at(old.behaviour);
                        if (previous.stop) previous.stop(context);
                        instances_.erase(found); found=instances_.end();
                    }
                    if (found==instances_.end())
                    {
                        Instance instance{object.id,script.id,script.behaviour,{},script.parameters};
                        auto& created=instances_.emplace(key,std::move(instance)).first->second;
                        ScriptContext context{object,created.parameters,created.state,0,&input,&pressed};
                        if (definition->second.start) definition->second.start(context);
                        found=instances_.find(key);
                    }
                    auto& instance=found->second; instance.parameters=script.parameters;
                    ScriptContext context{object,instance.parameters,instance.state,std::min(seconds,0.1),&input,&pressed};
                    definition->second.update(context);
                }
            for (auto iterator=instances_.begin();iterator!=instances_.end();)
            {
                if (live.contains(iterator->first)) { ++iterator; continue; }
                auto& instance=iterator->second;
                const auto owner=std::find_if(layout.objects.begin(),layout.objects.end(),[&](const auto& object) { return object.id==instance.owner; });
                if (owner!=layout.objects.end())
                {
                    ScriptContext context{*owner,instance.parameters,instance.state,0};
                    const auto& definition=Registry().at(instance.behaviour);
                    if (definition.stop) definition.stop(context);
                }
                iterator=instances_.erase(iterator);
            }
            error.clear(); return true;
        }
        catch (const std::exception& exception) { error=exception.what(); return false; }
        catch (...) { error="Script callback failed"; return false; }
    }
    void ScriptRuntime::Stop(SceneLayout& layout) noexcept
    {
        for (auto& [key,instance] : instances_)
        {
            static_cast<void>(key);
            try
            {
                const auto owner=std::find_if(layout.objects.begin(),layout.objects.end(),[&](const auto& object) { return object.id==instance.owner; });
                if (owner==layout.objects.end()) continue;
                ScriptContext context{*owner,instance.parameters,instance.state,0};
                const auto& definition=Registry().at(instance.behaviour);
                if (definition.stop) definition.stop(context);
            }
            catch (...) { Engine::Log::Warning("Script stop callback failed"); }
        }
        instances_.clear();
    }
}
