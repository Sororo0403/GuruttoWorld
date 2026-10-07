#pragma once
#include <SceneRuntime/SceneLayout.h>
#include <functional>
#include <map>
#include <set>

namespace SceneRuntime
{
    struct ScriptEvent
    {
        std::string name,sender,target;
        float value=0;
    };
    // Commands are applied after every callback; object pointers never survive a frame.
    class ScriptScene final
    {
    public:
        explicit ScriptScene(SceneLayout& layout,size_t& nextId) : layout_(layout),nextId_(nextId) {}
        const ScenePlacement* Find(const std::string& id) const;
        template<class T> const T* GetComponent(const std::string& id,std::optional<T> ScenePlacement::* member) const
        {
            const auto* found=Find(id);
            return found && (found->*member) ? &*(found->*member) : nullptr;
        }
        std::vector<std::string> FindByName(const std::string& name) const;
        std::string Spawn(ScenePlacement object);
        std::string Instantiate(const std::string& id,const std::array<float,3>& position);
        void Destroy(const std::string& id);
        void SetComponents(const std::string& id,const ScenePlacement& components);
        void Emit(ScriptEvent event);
        void Commit();
        std::vector<ScriptEvent> events;
    private:
        SceneLayout& layout_;
        size_t& nextId_;
        std::vector<ScenePlacement> spawned_;
        std::set<std::string> destroyed_;
        std::map<std::string,ScenePlacement> components_;
    };
    struct ScriptField { float initial=0,minimum=-1000000,maximum=1000000; };
    struct ScriptContext
    {
        ScenePlacement& object;
        const std::map<std::string,float>& parameters;
        std::map<std::string,float>& state;
        double seconds=0;
        const std::map<std::string,float>* input=nullptr;
        const std::map<std::string,bool>* pressed=nullptr;
        ScriptScene* scene=nullptr;
        const ScriptEvent* event=nullptr;
        float Input(const std::string& name) const;
        bool Pressed(const std::string& name) const;
        float Value(const std::string& name,float fallback=0) const;
    };
    struct ScriptDefinition
    {
        std::map<std::string,ScriptField> fields;
        std::function<void(ScriptContext&)> start,update,stop;
        std::function<void(ScriptContext&)> onEvent;
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
        // Transactional update. Use context.scene for structural changes; never resize arrays in a callback.
        bool Update(SceneLayout& layout,double seconds,std::string& error,
            const std::map<std::string,float>& input={},const std::map<std::string,bool>& pressed={});
        void Stop(SceneLayout& layout) noexcept;
        void QueueEvent(ScriptEvent event);
    private:
        bool Advance(SceneLayout& layout,double seconds,std::string& error,
            const std::map<std::string,float>& input,const std::map<std::string,bool>& pressed);
        struct Instance { std::string owner,id,behaviour; std::map<std::string,float> state,parameters; };
        std::map<std::pair<std::string,std::string>,Instance> instances_;
        size_t nextId_=1;
        std::vector<ScriptEvent> events_;
    };
}
