#include "AnimatorJson.h"
#include <limits>

namespace
{
    const char* TypeName(SceneRuntime::AnimatorBlendType type)
    {
        switch (type)
        {
        case SceneRuntime::AnimatorBlendType::OneDimensional: return "1D";
        case SceneRuntime::AnimatorBlendType::Cartesian2D: return "2D";
        case SceneRuntime::AnimatorBlendType::Direct: return "Direct";
        default: throw std::runtime_error("Unknown Blend Tree type");
        }
    }
    SceneRuntime::AnimatorBlendType ReadType(const std::string& name)
    {
        if (name=="1D") return SceneRuntime::AnimatorBlendType::OneDimensional;
        if (name=="2D") return SceneRuntime::AnimatorBlendType::Cartesian2D;
        if (name=="Direct") return SceneRuntime::AnimatorBlendType::Direct;
        throw std::runtime_error("Unknown Blend Tree type: "+name);
    }
    int Integer(const Engine::Json& value)
    {
        if (!value.is_number_integer()) throw std::runtime_error("Animation event integer must be an integer");
        if (value.is_number_unsigned())
        {
            const auto number=value.get<std::uint64_t>(); if (number>static_cast<std::uint64_t>((std::numeric_limits<int>::max)())) throw std::runtime_error("Animation event integer overflow");
            return static_cast<int>(number);
        }
        const auto number=value.get<std::int64_t>();
        if (number<(std::numeric_limits<int>::min)() || number>(std::numeric_limits<int>::max)()) throw std::runtime_error("Animation event integer overflow");
        return static_cast<int>(number);
    }
}
namespace SceneRuntime
{
    AnimatorComponent ReadAnimator(const Engine::Json& component,const std::string& id,bool enabled)
    {
        using Engine::JsonArray; using Engine::JsonNumber;
        AnimatorComponent animator; animator.id=id; animator.enabled=enabled;
        if (component.contains("parameters"))
        {
            const auto& parameters=Engine::JsonObject(component.at("parameters"));
            if (parameters.size()>64) throw std::runtime_error("Too many Animator parameters");
            for (const auto& [name,value] : parameters.items()) animator.parameters[name]=static_cast<float>(JsonNumber(value));
        }
        animator.initialState=component.at("initialState").get<std::string>(); animator.states.clear();
        const auto& states=JsonArray(component.at("states")); const auto& transitions=JsonArray(component.at("transitions"));
        if (states.size()>32 || transitions.size()>128) throw std::runtime_error("Too many Animator definitions");
        for (const auto& state : states)
        {
            AnimatorStateDefinition definition{state.at("name").get<std::string>(),state.at("clip").get<std::string>(),static_cast<float>(JsonNumber(state.at("speed"))),state.at("loop").get<bool>()};
            if (state.contains("blendTree")) definition.blendTree=state.at("blendTree").get<std::string>();
            animator.states.push_back(std::move(definition));
        }
        for (const auto& transition : transitions)
            animator.transitions.push_back({transition.at("from").get<std::string>(),transition.at("to").get<std::string>(),transition.at("parameter").get<std::string>(),
                transition.at("comparison").get<std::string>(),static_cast<float>(JsonNumber(transition.at("value"))),static_cast<float>(JsonNumber(transition.at("blendSeconds"))),static_cast<float>(JsonNumber(transition.at("exitTime")))});
        if (component.contains("events"))
        {
            const auto& events=JsonArray(component.at("events")); if (events.size()>256) throw std::runtime_error("Too many animation event keys");
            for (const auto& item : events)
            {
                Engine::JsonObject(item); AnimatorEventKey key;
                key.clip=item.at("clip").get<std::string>(); key.name=item.at("name").get<std::string>(); key.time=static_cast<float>(JsonNumber(item.at("time")));
                if (item.contains("value")) key.value=static_cast<float>(JsonNumber(item.at("value")));
                if (item.contains("minimumWeight")) key.minimumWeight=static_cast<float>(JsonNumber(item.at("minimumWeight")));
                if (item.contains("stringValue")) key.stringValue=item.at("stringValue").get<std::string>();
                if (item.contains("intValue")) key.intValue=Integer(item.at("intValue"));
                animator.events.push_back(std::move(key));
            }
        }
        if (component.contains("blendTrees"))
        {
            const auto& trees=JsonArray(component.at("blendTrees"));
            if (trees.size()>32) throw std::runtime_error("Too many Blend Trees");
            for (const auto& item : trees)
            {
                Engine::JsonObject(item);
                AnimatorBlendTree tree; tree.name=item.at("name").get<std::string>(); tree.type=ReadType(item.at("type").get<std::string>());
                if (item.contains("parameterX")) tree.parameterX=item.at("parameterX").get<std::string>();
                if (item.contains("parameterY")) tree.parameterY=item.at("parameterY").get<std::string>();
                const auto& children=JsonArray(item.at("children"));
                if (children.size()>32) throw std::runtime_error("Too many Blend Tree children");
                for (const auto& source : children)
                {
                    Engine::JsonObject(source);
                    AnimatorBlendMotion child;
                    if (source.contains("clip")) child.clip=source.at("clip").get<std::string>();
                    if (source.contains("blendTree")) child.blendTree=source.at("blendTree").get<std::string>();
                    if (source.contains("parameter")) child.parameter=source.at("parameter").get<std::string>();
                    if (source.contains("threshold")) child.threshold=static_cast<float>(JsonNumber(source.at("threshold")));
                    if (source.contains("speed")) child.speed=static_cast<float>(JsonNumber(source.at("speed")));
                    if (source.contains("position"))
                    {
                        const auto& position=JsonArray(source.at("position")); if (position.size()!=2) throw std::runtime_error("Blend Tree position requires two coordinates");
                        child.position={static_cast<float>(JsonNumber(position[0])),static_cast<float>(JsonNumber(position[1]))};
                    }
                    tree.children.push_back(std::move(child));
                }
                animator.blendTrees.push_back(std::move(tree));
            }
        }
        Animator::Validate(animator); return animator;
    }
    Engine::Json WriteAnimator(const AnimatorComponent& animator)
    {
        using Engine::Json;
        Animator::Validate(animator);
        Json object{{"id",animator.id},{"type","Animator"},{"enabled",animator.enabled},{"initialState",animator.initialState},{"states",Json::array()},{"transitions",Json::array()}};
        if (!animator.parameters.empty()) object["parameters"]=animator.parameters;
        if (!animator.events.empty())
        {
            object["events"]=Json::array();
            for (const auto& key : animator.events) object["events"].push_back({{"clip",key.clip},{"name",key.name},{"time",key.time},{"value",key.value},
                {"minimumWeight",key.minimumWeight},{"stringValue",key.stringValue},{"intValue",key.intValue}});
        }
        for (const auto& state : animator.states)
        {
            Json definition{{"name",state.name},{"clip",state.clip},{"speed",state.speed},{"loop",state.loop}};
            if (!state.blendTree.empty()) definition["blendTree"]=state.blendTree;
            object["states"].push_back(std::move(definition));
        }
        for (const auto& transition : animator.transitions) object["transitions"].push_back({{"from",transition.from},{"to",transition.to},{"parameter",transition.parameter},
            {"comparison",transition.comparison},{"value",transition.value},{"blendSeconds",transition.blendSeconds},{"exitTime",transition.exitTime}});
        if (!animator.blendTrees.empty())
        {
            object["blendTrees"]=Json::array();
            for (const auto& tree : animator.blendTrees)
            {
                Json definition{{"name",tree.name},{"type",TypeName(tree.type)},{"parameterX",tree.parameterX},{"parameterY",tree.parameterY},{"children",Json::array()}};
                for (const auto& child : tree.children) definition["children"].push_back({{"clip",child.clip},{"blendTree",child.blendTree},{"parameter",child.parameter},
                    {"threshold",child.threshold},{"position",child.position},{"speed",child.speed}});
                object["blendTrees"].push_back(std::move(definition));
            }
        }
        return object;
    }
}
