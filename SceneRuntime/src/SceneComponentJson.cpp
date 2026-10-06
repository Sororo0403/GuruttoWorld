#include "SceneComponentJson.h"
#include "EnvironmentJson.h"
#include "UiJson.h"
#include <algorithm>
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
            const bool enabled=component.at("enabled").get<bool>();
            if (type=="MeshRenderer")
            {
                if (placement.meshRenderer) throw std::runtime_error("Only one MeshRenderer is allowed");
                placement.meshRenderer=MeshRendererComponent{id,enabled,ReadModel(component)};
            }
            else if (type=="Rotator")
            {
                if (placement.rotator) throw std::runtime_error("Only one Rotator is allowed");
                placement.rotator=RotatorComponent{id,enabled,ReadVelocity(component)};
            }
            else if (!ReadEnvironmentComponent(component,placement,type) &&
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
        return components;
    }
}
