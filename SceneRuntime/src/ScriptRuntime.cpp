#include <SceneRuntime/ScriptRuntime.h>
#include <Engine/Core/Log.h>
#include <Engine/Core/Profiler.h>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace
{
    using namespace SceneRuntime;
    void ValidateUiKey(const std::string& key,size_t count,bool existing)
    {
        if(key.empty() || key.size()>128 || key.find_first_of("=&")!=std::string::npos || key.find('\0')!=std::string::npos || (!existing && count>=256))
            throw std::runtime_error("Invalid UI command key or command limit exceeded");
    }
    struct UiUtf8Prefix {size_t bytes;char32_t code,minimum;};
    UiUtf8Prefix ReadUiUtf8Prefix(unsigned char lead)
    {
        if(lead>=0xc2 && lead<=0xdf) return {1,static_cast<char32_t>(lead&0x1f),0x80};
        if(lead>=0xe0 && lead<=0xef) return {2,static_cast<char32_t>(lead&0x0f),0x800};
        if(lead>=0xf0 && lead<=0xf4) return {3,static_cast<char32_t>(lead&7),0x10000};
        throw std::runtime_error("UI text must be valid UTF-8");
    }
    void ValidateUiText(const std::string& text)
    {
        if(text.size()>4096 || text.find('\0')!=std::string::npos) throw std::runtime_error("Invalid UI text length");
        for(size_t index=0;index<text.size();) {
            const auto lead=static_cast<unsigned char>(text[index++]);if(lead<0x80) continue;
            auto prefix=ReadUiUtf8Prefix(lead);
            if(prefix.bytes>text.size()-index) throw std::runtime_error("UI text must be valid UTF-8");
            for(size_t i=0;i<prefix.bytes;++i) {const auto byte=static_cast<unsigned char>(text[index++]);if((byte&0xc0)!=0x80) throw std::runtime_error("UI text must be valid UTF-8");prefix.code=(prefix.code<<6)|(byte&0x3f);}
            if(prefix.code<prefix.minimum || prefix.code>0x10ffff || (prefix.code>=0xd800 && prefix.code<=0xdfff)) throw std::runtime_error("UI text must be valid UTF-8");
        }
    }
    ScriptDefinition FollowDefinition()
    {
        ScriptDefinition follow;
        follow.dataFields={{"active",{true}},{"target",{ScriptObjectReference{}}},
            {"offset",{ScriptValue::Object{{"x",{0.0f}},{"y",{0.0f}},{"z",{0.0f}}}}}};
        follow.update=[](ScriptContext& context) {
            if (!std::get<bool>(context.Data("active")->value)) return;
            const auto* target=context.Reference("target");
            if (!target) return;
            if (target->parentId!=context.object.parentId) throw std::runtime_error("FollowTarget requires the same parent as its target");
            const auto& offset=std::get<ScriptValue::Object>(context.Data("offset")->value);
            const auto position=target->position;
            const char* axes[]={"x","y","z"};
            for (size_t axis=0;axis<3;++axis) context.object.position[axis]=position[axis]+std::get<float>(offset.at(axes[axis]).value);
        };
        return follow;
    }
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
            ScriptDefinition pulse; pulse.fields={{"duration",{.2f,.01f,60}},{"peak",{15,0,10000}}};
            pulse.start=[](ScriptContext& context) { context.state["origin"]=context.object.pointLight ? context.object.pointLight->intensity : 0; context.state["remaining"]=0; context.state["scale"]=1; };
            pulse.onEvent=[](ScriptContext& context) {
                if (!context.event || !context.event->animation) return;
                context.state["events"]+=1; context.state["remaining"]=context.Value("duration",.2f);
                context.state["scale"]=context.event->animation->weight*std::clamp(std::abs(context.event->value),.1f,2.0f);
            };
            pulse.update=[](ScriptContext& context) {
                const float duration=context.Value("duration",.2f);
                context.state["remaining"]=std::max(0.0f,context.state["remaining"]-static_cast<float>(context.seconds));
                if (context.object.pointLight) context.object.pointLight->intensity=std::clamp(context.state["origin"]+context.Value("peak",15)*context.state["scale"]*context.state["remaining"]/duration,0.0f,10000.0f);
            };
            pulse.stop=[](ScriptContext& context) { if (context.object.pointLight) context.object.pointLight->intensity=context.state["origin"]; };
            result.emplace("AnimationEventPulse",std::move(pulse));
            ScriptDefinition orbit; orbit.fields={{"radius",{.25f,0,1000}},{"frequency",{.3f,0,1000}},{"weight",{1,0,1}}};
            orbit.update=[](ScriptContext& context) {
                if (!context.scene || !context.object.animator || context.object.animator->ik.empty()) return;
                const auto& item=context.object.animator->ik.front();
                context.state["phase"]=std::remainder(context.state["phase"]+static_cast<float>(context.seconds)*context.Value("frequency",.3f)*6.2831853f,6.2831853f);
                AnimatorIkTarget target{item.target,item.hint,context.Value("weight",1),item.worldSpace};
                target.target[0]+=std::sin(context.state["phase"])*context.Value("radius",.25f);
                target.target[2]+=std::cos(context.state["phase"])*context.Value("radius",.25f);
                context.scene->SetIkTarget(context.object.id,item.name,target);
            };
            result.emplace("IkOrbit",std::move(orbit));
            ScriptDefinition rootMotion; rootMotion.fields={{"enabled",{1,-1,1}}};
            rootMotion.update=[](ScriptContext& context) {
                if (!context.scene || !context.object.animator) return;
                const float enabled=context.Value("enabled",1);
                context.scene->SetRootMotion(context.object.id,enabled<0 ? std::nullopt : std::optional<bool>(enabled>0));
            };
            result.emplace("RootMotionControl",std::move(rootMotion));
            result.emplace("FollowTarget",FollowDefinition());
            return result;
        }();
        return definitions;
    }
    void Validate(const ScriptComponent& script,const ScriptDefinition& definition)
    {
        size_t remaining=4096; ScriptValue{script.data}.Validate(0,remaining);
        for (const auto& [name,value]:script.data) {
            const auto field=definition.dataFields.find(name);
            if (field==definition.dataFields.end() || field->second.value.index()!=value.value.index())
                throw std::runtime_error("Unknown or incompatible script data field: "+name);
        }
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
        for(auto& [name,ids]:layout_.sceneObjects)
        {
            static_cast<void>(name);
            if(std::find(ids.begin(),ids.end(),currentOwner)!=ids.end()) {ids.push_back(object.id);break;}
        }
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
            if(item.joint) remap(item.joint->target);
            for (auto& script:item.scripts) script.Remap(ids);
        if(item.navAgent) item.navAgent->Remap(ids);
            if(item.ragdoll) item.ragdoll->Remap(ids);
            if (item.button)
            {
                if (item.button->action!="loadScene" && item.button->action!="loadSceneAdditive" && item.button->action!="unloadScene" && item.button->action!="setState") remap(item.button->target);
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
        if (event.animation) AnimationEvents::ValidateOccurrence(*event.animation);
        if (events.size()>=4096 || event.name.empty() || event.name.size()>128 || event.text.size()>4096 || !std::isfinite(event.value))
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
        if(uiCommands.values.size()+uiCommands.texts.size()>256) throw std::runtime_error("Too many UI commands");
        for(const auto& [key,value]:uiCommands.values) SetUiValue(key,value);
        for(const auto& [key,text]:uiCommands.texts) SetUiText(key,text);
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
        std::erase_if(ikTargets,[&](const auto& changes) { const auto* object=Find(changes.first); return !object || !object->animator; });
        std::erase_if(rootMotions,[&](const auto& changes) { const auto* object=Find(changes.first); return !object || !object->animator; });
        if (destroyed_.contains(layout_.settings.mainCamera)) layout_.settings.mainCamera.clear();
        static_cast<void>(layout_.Serialize());
    }
    void ScriptScene::SetUiValue(const std::string& key,float value)
    {
        ValidateUiKey(key,uiCommands.values.size()+uiCommands.texts.size(),uiCommands.values.contains(key));
        if(!std::isfinite(value) || std::abs(value)>100000) throw std::runtime_error("Invalid UI value command");
        uiCommands.values[key]=value;
    }
    void ScriptScene::SetUiText(const std::string& key,std::string text)
    {
        ValidateUiKey(key,uiCommands.values.size()+uiCommands.texts.size(),uiCommands.texts.contains(key));
        ValidateUiText(text);
        uiCommands.texts[key]=std::move(text);
    }
    void ScriptRuntime::QueueEvent(ScriptEvent event)
    {
        if (event.animation) AnimationEvents::ValidateOccurrence(*event.animation);
        if (events_.size()>=4096 || event.name.empty() || event.name.size()>128 || event.text.size()>4096 || !std::isfinite(event.value))
            throw std::runtime_error("Invalid script event or event limit exceeded");
        events_.push_back(std::move(event));
    }
    void ScriptScene::SetRootMotion(const std::string& id,std::optional<bool> enabled)
    {
        const auto* object=Find(id);
        if (!object || !object->animator) throw std::runtime_error("Root motion owner missing: "+id);
        if (enabled.value_or(false) && object->animator->rootBone.empty()) throw std::runtime_error("Root motion bone missing: "+id);
        rootMotions[id]=enabled;
    }
    void ScriptScene::SetAnimatorParameter(const std::string& id,const std::string& name,float value)
    {
        const auto* object=Find(id); if (!object || !object->animator) throw std::runtime_error("Animator parameter owner missing: "+id);
        const auto found=animatorParameters.find(id);
        auto values=found==animatorParameters.end() ? std::map<std::string,float>{} : found->second; values[name]=value;
        Animator::ValidateParameters(values); animatorParameters[id]=std::move(values);
    }
    void ScriptScene::SetIkTarget(const std::string& id,const std::string& name,const AnimatorIkTarget& target)
    {
        Animator::ValidateIkTarget(target);
        ClearIkTarget(id,name); ikTargets[id][name]=target;
    }
    void ScriptScene::ClearIkTarget(const std::string& id,const std::string& name)
    {
        const auto* object=Find(id);
        if (!object || !object->animator || std::ranges::none_of(object->animator->ik,[&](const auto& item) { return item.name==name; }))
            throw std::runtime_error("IK constraint owner missing: "+id+"/"+name);
        ikTargets[id][name]=std::nullopt;
    }
    float ScriptContext::Value(const std::string& name,float fallback) const
    { const auto found=parameters.find(name); return found==parameters.end() ? fallback : found->second; }
    const ScriptValue* ScriptContext::Data(const std::string& name) const
    { if (!data) return nullptr; const auto found=data->find(name); return found==data->end() ? nullptr : &found->second; }
    const ScenePlacement* ScriptContext::Reference(const std::string& name) const
    {
        const auto field=Data(name);
        const auto reference=field ? std::get_if<ScriptObjectReference>(&field->value) : nullptr;
        return reference && scene ? scene->Find(reference->id) : nullptr;
    }
    bool ValidScriptField(const std::string& key,const ScriptField& field)
    {
        return !key.empty() && key.size()<=128 && key.find('\0')==std::string::npos &&
            std::isfinite(field.initial) && std::isfinite(field.minimum) && std::isfinite(field.maximum) &&
            field.initial>=field.minimum && field.initial<=field.maximum && field.minimum>=-1000000 && field.maximum<=1000000;
    }
    bool ValidDefinition(const std::string& name,const ScriptDefinition& definition)
    {
        if (name.empty() || name.size()>128 || name.find('\0')!=std::string::npos || definition.fields.size()>64 ||
            (!definition.update && !definition.fixedUpdate && !definition.lateUpdate)) return false;
        try { size_t remaining=4096; ScriptValue{definition.dataFields}.Validate(0,remaining); }
        catch (const std::exception&) { return false; }
        for (const auto& [key,field] : definition.fields)
            if (!ValidScriptField(key,field)) return false;
        return true;
    }
    bool ScriptRegistry::Register(std::string name,ScriptDefinition definition)
    {
        return ValidDefinition(name,definition) && Registry().emplace(std::move(name),std::move(definition)).second;
    }
    bool ScriptRegistry::InstallModule(const std::string& owner,std::map<std::string,ScriptDefinition> definitions,std::string& error)
    {
        static std::map<std::string,std::set<std::string>> owners;
        try {
            if (owner.empty() || owner.size()>32768 || definitions.size()>256) throw std::runtime_error("Invalid script module owner or size");
            auto candidate=Registry(); auto candidateOwners=owners;
            const auto previous=candidateOwners.find(owner);
            if (previous!=candidateOwners.end()) for (const auto& name:previous->second) candidate.erase(name);
            std::set<std::string> names;
            for (auto& [name,definition]:definitions) {
                if (!ValidDefinition(name,definition)) throw std::runtime_error("Invalid script module definition: "+name);
                if (candidate.contains(name)) throw std::runtime_error("Script module name conflicts with an existing behaviour: "+name);
                names.insert(name); candidate.emplace(name,std::move(definition));
            }
            candidateOwners[owner]=std::move(names); Registry().swap(candidate); owners.swap(candidateOwners); error.clear(); return true;
        } catch (const std::exception& exception) { error=exception.what(); return false; }
    }
    const std::map<std::string,ScriptDefinition>& ScriptRegistry::Definitions() { return Registry(); }
    float ScriptContext::Input(const std::string& name) const
    { if (!input) return 0; const auto found=input->find(name); return found==input->end() ? 0 : found->second; }
    bool ScriptContext::Pressed(const std::string& name) const
    { if (!pressed) return false; const auto found=pressed->find(name); return found!=pressed->end() && found->second; }
    bool ScriptRuntime::HasPhase(const SceneLayout& layout,ScriptPhase phase)
    {
        for (const auto& object:layout.objects) for (const auto& script:object.scripts) {
            if (!script.enabled) continue;
            const auto found=Registry().find(script.behaviour);
            if (found==Registry().end()) continue;
            const auto& definition=found->second;
            if (phase==ScriptPhase::FixedUpdate && definition.fixedUpdate) return true;
            if (phase==ScriptPhase::LateUpdate && definition.lateUpdate) return true;
            if (phase==ScriptPhase::Update && definition.update) return true;
        }
        return false;
    }
    bool ScriptRuntime::Update(SceneLayout& layout,double seconds,std::string& error,const std::map<std::string,float>& input,const std::map<std::string,bool>& pressed,const PhysicsWorld* physics,ScriptPhase phase)
    {
        Engine::CpuScope scope("Scripts");
        if (!std::isfinite(seconds) || seconds<=0) { error="Invalid script time"; return false; }
        if (instances_.empty() && events_.empty() && std::none_of(layout.objects.begin(),layout.objects.end(),[](const auto& item) { return !item.scripts.empty(); }))
        { impulses_.clear(); animatorParameters_.clear(); ikTargets_.clear(); rootMotions_.clear(); error.clear(); return true; }
        auto candidate=layout;
        auto runtime=*this;
        if (!runtime.Advance(candidate,seconds,error,input,pressed,physics,phase)) return false;
        layout=std::move(candidate); *this=std::move(runtime); return true;
    }
    bool ScriptRuntime::Advance(SceneLayout& layout,double seconds,std::string& error,const std::map<std::string,float>& input,const std::map<std::string,bool>& pressed,const PhysicsWorld* physics,ScriptPhase phase)
    {
        if (!std::isfinite(seconds) || seconds<=0) { error="Invalid script time"; return false; }
        try
        {
            ScriptScene scene(layout,nextId_,physics);
            scene.sceneCommands=sceneCommands_;
            scene.uiCommands=uiCommands_;
            if (phase!=ScriptPhase::Update) scene.events=events_;
            if (pendingFixedCommands_ && phase!=ScriptPhase::LateUpdate) {
                scene.animatorParameters=animatorParameters_; scene.ikTargets=ikTargets_; scene.rootMotions=rootMotions_;
            }
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
                        context.data=&old.data; context.dataState=&old.dataState;
                        const auto& previous=Registry().at(old.behaviour);
                        if (previous.stop) previous.stop(context);
                        instances_.erase(found); found=instances_.end();
                    }
                    if (found==instances_.end())
                    {
                        Instance instance{object.id,script.id,script.behaviour,{},script.parameters,definition->second.dataFields,{}};
                        for (const auto& [name,value]:script.data) instance.data[name]=value;
                        auto& created=instances_.emplace(key,std::move(instance)).first->second;
                        ScriptContext context{object,created.parameters,created.state,0,&input,&pressed,&scene};
                        context.data=&created.data; context.dataState=&created.dataState;
                        context.phase=phase;
                        if (definition->second.start) definition->second.start(context);
                        found=instances_.find(key);
                    }
                    auto& instance=found->second; instance.parameters=script.parameters;
                    ScriptContext context{object,instance.parameters,instance.state,std::min(seconds,0.1),&input,&pressed,&scene};
                    instance.data=definition->second.dataFields;
                    for (const auto& [name,value]:script.data) instance.data[name]=value;
                    context.data=&instance.data; context.dataState=&instance.dataState;
                    context.phase=phase;
                    if (phase==ScriptPhase::Update && definition->second.onEvent)
                        for (const auto& event : events_) if (event.target.empty() || event.target==object.id)
                        { context.event=&event; definition->second.onEvent(context); }
                    context.event=nullptr;
                    const auto& callback=phase==ScriptPhase::FixedUpdate ? definition->second.fixedUpdate :
                        phase==ScriptPhase::LateUpdate ? definition->second.lateUpdate : definition->second.update;
                    if (callback) callback(context);
                    size_t remaining=4096; ScriptValue{instance.dataState}.Validate(0,remaining);
                }
            for (auto iterator=instances_.begin();iterator!=instances_.end();)
            {
                if (live.contains(iterator->first)) { ++iterator; continue; }
                auto& instance=iterator->second;
                const auto owner=std::find_if(layout.objects.begin(),layout.objects.end(),[&](const auto& object) { return object.id==instance.owner; });
                if (owner!=layout.objects.end())
                {
                    ScriptContext context{*owner,instance.parameters,instance.state,0};
                    context.data=&instance.data; context.dataState=&instance.dataState;
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
                    { ScriptContext context{*owner,instance.parameters,instance.state,0}; context.data=&instance.data; context.dataState=&instance.dataState; definition.stop(context); }
                    iterator=instances_.erase(iterator);
                }
            events_=std::move(scene.events);
            if(scene.sceneCommands.size()>64) throw std::runtime_error("Too many scene commands");
            sceneCommands_=std::move(scene.sceneCommands);
            uiCommands_=std::move(scene.uiCommands);
            impulses_=std::move(scene.impulses);
            animatorParameters_=std::move(scene.animatorParameters);
            ikTargets_=std::move(scene.ikTargets);
            rootMotions_=std::move(scene.rootMotions);
            pendingFixedCommands_=phase==ScriptPhase::FixedUpdate;
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
                context.data=&instance.data; context.dataState=&instance.dataState;
                const auto& definition=Registry().at(instance.behaviour);
                if (definition.stop) definition.stop(context);
            }
            catch (...) { Engine::Log::Warning("Script stop callback failed"); }
        }
        instances_.clear();
        sceneCommands_.clear();
        uiCommands_={};
        pendingFixedCommands_=false;
        events_.clear(); impulses_.clear(); animatorParameters_.clear(); ikTargets_.clear(); rootMotions_.clear(); nextId_=1;
    }
    void ScriptRuntime::StopRemoved(SceneLayout& previous,const SceneLayout& next) noexcept
    {
        for(auto iterator=instances_.begin();iterator!=instances_.end();)
        {
            const auto& instance=iterator->second;
            const bool survives=std::any_of(next.objects.begin(),next.objects.end(),[&](const auto& object){return object.id==instance.owner &&
                std::any_of(object.scripts.begin(),object.scripts.end(),[&](const auto& script){return script.enabled && script.id==instance.id && script.behaviour==instance.behaviour;});});
            if(survives) {++iterator;continue;}
            try
            {
                const auto owner=std::find_if(previous.objects.begin(),previous.objects.end(),[&](const auto& object){return object.id==instance.owner;});
                const auto definition=Registry().find(instance.behaviour);
                if(owner!=previous.objects.end() && definition!=Registry().end() && definition->second.stop)
                {
                    auto& state=iterator->second; ScriptContext context{*owner,state.parameters,state.state,0};
                    context.data=&state.data;context.dataState=&state.dataState;definition->second.stop(context);
                }
            }
            catch(...) {Engine::Log::Warning("Script stop callback failed while unloading a scene");}
            iterator=instances_.erase(iterator);
        }
    }
}
