#include "SceneSettingsJson.h"
#include <cmath>
#include <stdexcept>

namespace
{
    using Engine::Json;
    using Engine::JsonNumber;
    using Engine::JsonArray;
    using Engine::JsonObject;
    template<size_t N>
    std::array<float,N> Color(const Json& object, const char* key)
    {
        const auto& values=JsonArray(object.at(key));
        if (values.size()!=N) throw std::runtime_error("Scene color has wrong size");
        std::array<float,N> result{};
        for (uint32_t index=0;index<N;++index)
        {
            const auto value=JsonNumber(values.at(index));
            if (!std::isfinite(value) || value<0 || value>1) throw std::runtime_error("Scene color must be within [0,1]");
            result[index]=static_cast<float>(value);
        }
        return result;
    }
    float Number(const Json& object, const char* key, float low, float high)
    {
        const auto value=JsonNumber(object.at(key));
        if (!std::isfinite(value) || value<low || value>high) throw std::runtime_error("Fog property out of range");
        return static_cast<float>(value);
    }
    template<size_t N>
    Json Array(const std::array<float,N>& values)
    {
        return Json(values);
    }
}
namespace SceneRuntime
{
    SceneSettings ReadSceneSettings(const Json& object)
    {
        SceneSettings result;
        result.background=Color<4>(object,"background");
        result.mainCamera=object.at("mainCamera").get<std::string>();
        if (object.contains("fog"))
        {
            const auto& fog=JsonObject(object.at("fog"));
            result.fog.enabled=fog.at("enabled").get<bool>();
            result.fog.color=Color<3>(fog,"color");
            result.fog.start=Number(fog,"start",0,1000000);
            result.fog.end=Number(fog,"end",0.001f,1000000);
            result.fog.strength=Number(fog,"strength",0,1);
            if (result.fog.end-result.fog.start<=0.00001f) throw std::runtime_error("Fog end must exceed start");
        }
        return result;
    }
    Json WriteSceneSettings(const SceneSettings& settings)
    {
        Json result,fog;
        result["background"]=Array(settings.background);
        result["mainCamera"]=settings.mainCamera;
        fog["enabled"]=settings.fog.enabled;
        fog["color"]=Array(settings.fog.color);
        fog["start"]=settings.fog.start;
        fog["end"]=settings.fog.end;
        fog["strength"]=settings.fog.strength;
        result["fog"]=fog;
        return result;
    }
}
