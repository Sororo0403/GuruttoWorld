#include "SceneComponentJson.h"
#include "EnvironmentJson.h"
#include "UiJson.h"
#include <algorithm>
#include <iterator>
#include <cmath>
#include <cctype>
#include <stdexcept>
#include <unordered_set>

namespace
{
    using Engine::Json;
    using Engine::JsonArray;
    using Engine::JsonObject;
    using Engine::JsonNumber;
    std::filesystem::path ReadModel(const Json& object)
    {
        const auto text=object.at("model").get<std::string>();
        const auto path=std::filesystem::path(std::u8string(text.begin(),text.end()));
        auto extension=path.extension().string();
        std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (path.is_absolute() || path.has_root_name() || !text.starts_with("Assets/Models/") ||
            extension!=".obj" || std::any_of(path.begin(),path.end(),[](const auto& part) { return part==".."; }))
            throw std::runtime_error("MeshRenderer model must be an OBJ relative to Assets/Models");
        return path;
    }

    std::array<float,3> ReadVelocity(const Json& object)
    {
        const auto& array=JsonArray(object.at("angularVelocity"));
        if (array.size()!=3) throw std::runtime_error("Rotator angularVelocity requires three components");
        std::array<float,3> result;
        for (uint32_t index=0;index<3;++index)
        {
            const auto value=JsonNumber(array.at(index));
            if (!std::isfinite(value) || std::abs(value)>100000.0) throw std::runtime_error("Rotator speed must be finite and within +/-100000 degrees/s");
            result[index]=static_cast<float>(value);
        }
        return result;
    }

    Json Component(const std::string& id, const char* type, bool enabled)
    {
        Json object=Json::object();
        object["id"]=id;
        object["type"]=type;
        object["enabled"]=enabled;
        return object;
    }
    SceneRuntime::AnimationComponent ReadAnimation(const Json& object)
    {
        SceneRuntime::AnimationComponent animation;
        animation.id=object.at("id").get<std::string>();
        animation.enabled=object.at("enabled").get<bool>();
        for (const auto& value : JsonArray(object.at("tracks")))
        {
            SceneRuntime::AnimationTrack track;
            track.property=value.at("property").get<std::string>();
            track.clock=value.at("clock").get<std::string>();
            track.easing=value.at("easing").get<std::string>();
            track.delay=static_cast<float>(JsonNumber(value.at("delay")));
            track.loop=value.at("loop").get<bool>();
            track.keys.clear();
            for (const auto& key : JsonArray(value.at("keys")))
            {
                SceneRuntime::AnimationKey frame;
                frame.time=static_cast<float>(JsonNumber(key.at("time")));
                const auto& vector=JsonArray(key.at("value"));
                if (vector.size()!=4) throw std::runtime_error("Animation value requires four numbers");
                for (size_t axis=0;axis<4;++axis) frame.value[axis]=static_cast<float>(JsonNumber(vector.at(axis)));
                track.keys.push_back(frame);
            }
            if (!SceneRuntime::Animation::Valid(track)) throw std::runtime_error("Invalid animation track");
            animation.tracks.push_back(std::move(track));
        }
        if (animation.tracks.size()>32) throw std::runtime_error("Too many animation tracks");
        return animation;
    }
    Json WriteAnimation(const SceneRuntime::AnimationComponent& animation)
    {
        auto object=Component(animation.id,"Animation",animation.enabled);
        object["tracks"]=Json::array();
        for (const auto& track : animation.tracks)
        {
            Json keys=Json::array();
            std::transform(track.keys.begin(),track.keys.end(),std::back_inserter(keys),[](const auto& key) {
                return Json{{"time",key.time},{"value",key.value}};
            });
            object["tracks"].push_back({{"property",track.property},{"clock",track.clock},
                {"easing",track.easing},{"delay",track.delay},{"loop",track.loop},{"keys",keys}});
        }
        return object;
    }
    bool ReadBasicComponent(const Json& component,SceneRuntime::ScenePlacement& placement,const std::string& type)
    {
        const auto id=component.at("id").get<std::string>();
        const bool enabled=component.at("enabled").get<bool>();
        if (type=="MeshRenderer")
        {
            if (placement.meshRenderer) throw std::runtime_error("Only one MeshRenderer is allowed");
            placement.meshRenderer=SceneRuntime::MeshRendererComponent{id,enabled,ReadModel(component)};
        }
        else if (type=="Rotator")
        {
            if (placement.rotator) throw std::runtime_error("Only one Rotator is allowed");
            placement.rotator=SceneRuntime::RotatorComponent{id,enabled,ReadVelocity(component)};
        }
        else if (type=="Animation")
        {
            if (placement.animation) throw std::runtime_error("Only one Animation is allowed");
            placement.animation=ReadAnimation(component);
        }
        else return false;
        return true;
    }
}

namespace SceneRuntime
{
    void ReadSceneComponents(const Json& object, ScenePlacement& placement, bool legacy)
    {
        if (legacy)
        {
            if (object.contains("components")) throw std::runtime_error("Version 2 cannot contain components");
            placement.SetModel(ReadModel(object));
            return;
        }
        if (object.contains("model")) throw std::runtime_error("Version 3 model belongs to MeshRenderer");
        std::unordered_set<std::string> ids{"transform"};
        for (const auto& value : JsonArray(object.at("components")))
        {
            const auto& component=JsonObject(value);
            const auto id=component.at("id").get<std::string>();
            if (id.empty() || id.find('\0')!=std::string::npos || !ids.insert(id).second)
                throw std::runtime_error("Empty, duplicate or reserved component ID");
            const auto type=component.at("type").get<std::string>();
            if (!ReadBasicComponent(component,placement,type) && !ReadEnvironmentComponent(component,placement,type) &&
                !ReadUiComponent(component,placement,type))
                throw std::runtime_error("Unsupported component type: "+type);
        }
    }

    Json WriteSceneComponents(const ScenePlacement& placement)
    {
        Json components=Json::array();
        if (placement.meshRenderer)
        {
            const auto& mesh=*placement.meshRenderer;
            auto object=Component(mesh.id,"MeshRenderer",mesh.enabled);
            object["model"]=mesh.model;
            components.push_back(object);
        }
        if (placement.rotator)
        {
            const auto& rotator=*placement.rotator;
            auto object=Component(rotator.id,"Rotator",rotator.enabled);
            Json velocity=Json::array();
            for (const auto value : rotator.angularVelocity)
            {
                if (!std::isfinite(value) || std::abs(value)>100000.0f) throw std::runtime_error("Invalid Rotator speed");
                velocity.push_back(value);
            }
            object["angularVelocity"]=velocity;
            components.push_back(object);
        }
        WriteEnvironmentComponents(components,placement);
        WriteUiComponents(components,placement);
        if (placement.animation) components.push_back(WriteAnimation(*placement.animation));
        return components;
    }
}
