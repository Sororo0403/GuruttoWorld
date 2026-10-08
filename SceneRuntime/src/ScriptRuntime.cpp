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
    const ScenePlacement* ScriptScene::Find(const std::string& id) const
    {
        const auto found=std::find_if(layout_.objects.begin(),layout_.objects.end(),[&](const auto& item) { return item.id==id; });
        return found==layout_.objects.end() ? nullptr : &*found;
    }
    std::vector<std::string> ScriptScene::FindByName(const std::string& name) const
    {
        std::vector<std::string> result;
        for (const auto& item : layout_.objects) if (item.name==name) result.push_back(item.id);
        return result;
    }
    std::string ScriptScene::Spawn(ScenePlacement object)
    {
        if (spawned_.size()>=4096) throw std::runtime_error("Script spawn limit exceeded");
        do { object.id="runtime-"+std::to_string(nextId_++); }
        while (Find(object.id) || std::any_of(spawned_.begin(),spawned_.end(),[&](const auto& item) { return item.id==object.id; }));
        object.prefab.reset();
        const auto id=object.id; spawned_.push_back(std::move(object)); return id;
    }
    std::string ScriptScene::Instantiate(const std::string& id,const std::array<float,3>& position)
    {
        const auto* source=Find(id);
        if (!source) throw std::runtime_error("Instantiate source missing: "+id);
        std::set<std::string> selected{id};
        bool changed=true;
        while (changed)
        {
            changed=false;
            for (const auto& item : layout_.objects)
                if (selected.contains(item.parentId) && selected.insert(item.id).second) changed=true;
        }
        std::map<std::string,std::string> ids;
        const auto begin=spawned_.size();
        for (auto item : layout_.objects) if (selected.contains(item.id))
        {
            const auto original=item.id;
            if (original==id) item.position=position;
            ids[original]=Spawn(std::move(item));
        }
        for (size_t i=begin;i<spawned_.size();++i)
        {
            auto& item=spawned_[i];
            const auto remap=[&](std::string& value) { const auto found=ids.find(value); if (found!=ids.end()) value=found->second; };
            remap(item.parentId);
            if (item.button)
            {
                if (item.button->action!="loadScene" && item.button->action!="setState") remap(item.button->target);
                remap(item.button->sound);
            }
        }
        return ids.at(id);
    }
    void ScriptScene::Destroy(const std::string& id) { destroyed_.insert(id); }
    void ScriptScene::SetComponents(const std::string& id,const ScenePlacement& components)
    {
        if (!Find(id)) throw std::runtime_error("Component owner missing: "+id);
        components_[id]=components;
    }
    void ScriptScene::Emit(ScriptEvent event)
    {
        if (events.size()>=4096 || event.name.empty() || event.name.size()>128 || !std::isfinite(event.value))
            throw std::runtime_error("Invalid script event or event limit exceeded");
        events.push_back(std::move(event));
    }
    void ScriptScene::AddImpulse(const std::string& id,const std::array<float,3>& impulse)
    {
        const auto* object=Find(id);
        if (!object || !object->rigidBody || !object->rigidBody->enabled || object->rigidBody->motion!="dynamic") throw std::runtime_error("Impulse requires a dynamic rigid body");
        auto amount=impulses[id];
        for (size_t i=0;i<3;++i)
        {
            amount[i]+=impulse[i];
            if (!std::isfinite(amount[i]) || std::abs(amount[i])>1000000) throw std::runtime_error("Invalid impulse");
        }
        impulses[id]=amount;
    }
    void ScriptScene::Commit()
    {
        for (auto& item : layout_.objects)
        {
            const auto found=components_.find(item.id);
            if (found!=components_.end()) item.CopyComponents(found->second);
        }
        for (auto& item : spawned_) layout_.objects.push_back(std::move(item));
        bool changed=true;
        while (changed)
        {
            changed=false;
            for (const auto& item : layout_.objects)
                if (destroyed_.contains(item.parentId) && destroyed_.insert(item.id).second) changed=true;
        }
        std::erase_if(layout_.objects,[&](const auto& item) { return destroyed_.contains(item.id); });
        std::erase_if(animatorParameters,[&](const auto& changes) { const auto* object=Find(changes.first); return !object || !object->animator; });
        if (destroyed_.contains(layout_.settings.mainCamera)) layout_.settings.mainCamera.clear();
        static_cast<void>(layout_.Serialize());
    }
    void ScriptRuntime::QueueEvent(ScriptEvent event)
    {
        if (events_.size()>=4096 || event.name.empty() || event.name.size()>128 || !std::isfinite(event.value))
            throw std::runtime_error("Invalid script event or event limit exceeded");
        events_.push_back(std::move(event));
    }
    void ScriptScene::SetAnimatorParameter(const std::string& id,const std::string& name,float value)
    {
        const auto* object=Find(id); if (!object || !object->animator) throw std::runtime_error("Animator parameter owner missing: "+id);
        const auto found=animatorParameters.find(id);
        auto values=found==animatorParameters.end() ? std::map<std::string,float>{} : found->second; values[name]=value;
        Animator::ValidateParameters(values); animatorParameters[id]=std::move(values);
    }
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
    bool ScriptRuntime::Update(SceneLayout& layout,double seconds,std::string& error,const std::map<std::string,float>& input,const std::map<std::string,bool>& pressed,const PhysicsWorld* physics)
    {
        if (!std::isfinite(seconds) || seconds<=0) { error="Invalid script time"; return false; }
        if (instances_.empty() && events_.empty() && std::none_of(layout.objects.begin(),layout.objects.end(),[](const auto& item) { return !item.scripts.empty(); }))
        { impulses_.clear(); animatorParameters_.clear(); error.clear(); return true; }
        auto candidate=layout;
        auto runtime=*this;
        if (!runtime.Advance(candidate,seconds,error,input,pressed,physics)) return false;
        layout=std::move(candidate); *this=std::move(runtime); return true;
    }
    bool ScriptRuntime::Advance(SceneLayout& layout,double seconds,std::string& error,const std::map<std::string,float>& input,const std::map<std::string,bool>& pressed,const PhysicsWorld* physics)
    {
        if (!std::isfinite(seconds) || seconds<=0) { error="Invalid script time"; return false; }
        try
        {
            ScriptScene scene(layout,nextId_,physics);
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
                        ScriptContext context{object,created.parameters,created.state,0,&input,&pressed,&scene};
                        if (definition->second.start) definition->second.start(context);
                        found=instances_.find(key);
                    }
                    auto& instance=found->second; instance.parameters=script.parameters;
                    ScriptContext context{object,instance.parameters,instance.state,std::min(seconds,0.1),&input,&pressed,&scene};
                    if (definition->second.onEvent)
                        for (const auto& event : events_) if (event.target.empty() || event.target==object.id)
                        { context.event=&event; definition->second.onEvent(context); }
                    context.event=nullptr;
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
            auto previous=layout;
            scene.Commit();
            for (auto iterator=instances_.begin();iterator!=instances_.end();)
                if (scene.Find(iterator->second.owner)) ++iterator;
                else
                {
                    auto& instance=iterator->second;
                    const auto owner=std::find_if(previous.objects.begin(),previous.objects.end(),[&](const auto& item) { return item.id==instance.owner; });
                    const auto& definition=Registry().at(instance.behaviour);
                    if (owner!=previous.objects.end() && definition.stop)
                    { ScriptContext context{*owner,instance.parameters,instance.state,0}; definition.stop(context); }
                    iterator=instances_.erase(iterator);
                }
            events_=std::move(scene.events);
            impulses_=std::move(scene.impulses);
            animatorParameters_=std::move(scene.animatorParameters);
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
        events_.clear(); impulses_.clear(); animatorParameters_.clear(); nextId_=1;
    }
}
