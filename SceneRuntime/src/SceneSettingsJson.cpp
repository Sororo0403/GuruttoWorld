#include <Engine/Core/Validation.h>
#include "SceneSettingsJson.h"
#include <iterator>
#include <algorithm>
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
            if (!Engine::InRange(value,0,1)) throw std::runtime_error("Scene color must be within [0,1]");
            result[index]=static_cast<float>(value);
        }
        return result;
    }
    float Number(const Json& object, const char* key, float low, float high)
    {
        const auto value=JsonNumber(object.at(key));
        if (!std::isfinite(value) || value<low || value>high) throw std::runtime_error("Scene property out of range");
        return static_cast<float>(value);
    }
    template<size_t N>
    Json Array(const std::array<float,N>& values)
    {
        return Json(values);
    }
    void ReadInputs(const Json& object,SceneRuntime::SceneSettings& result)
    {
        if(object.contains("playerInputs")) result.playerInputs=object.at("playerInputs").get<std::array<std::string,5>>();
        if(std::any_of(result.playerInputs.begin(),result.playerInputs.end(),[](const auto& action) {
            return action.empty() || action.size()>128 || action.find('\0')!=std::string::npos;
        })) throw std::runtime_error("Invalid player input action");
    }
    void ReadPhysics(const Json& object,SceneRuntime::SceneSettings& result)
    {
        if(object.contains("gravity")) {
            result.gravity=object.at("gravity").get<std::array<float,3>>();
            if (std::any_of(std::begin(result.gravity),std::end(result.gravity),[](const auto value) {return !Engine::InRange(value,-1000,1000);})) throw std::runtime_error("Invalid world gravity");
        }
        if(object.contains("fixedStep")) result.fixedStep=Number(object,"fixedStep",1.0f/240.0f,.1f);
        if(object.contains("maximumFixedSteps")) {const auto count=Number(object,"maximumFixedSteps",1,32);if(std::floor(count)!=count) throw std::runtime_error("Invalid fixed step count");result.maximumFixedSteps=static_cast<unsigned int>(count);}
    }
    void ReadRendering(const Json& object,SceneRuntime::SceneSettings& result)
    {
        for(const auto& [key,target]:{std::pair{"shadowResolution",&result.shadowResolution},std::pair{"localShadowResolution",&result.localShadowResolution}})
            if(object.contains(key)) {const auto value=Number(object,key,128,std::string_view(key)=="shadowResolution"?4096.0f:2048.0f);const auto size=static_cast<unsigned int>(value);if(value!=size || (size&(size-1))!=0) throw std::runtime_error("Shadow resolution must be a power of two");*target=size;}
        if(object.contains("sceneRenderScale")) result.sceneRenderScale=Number(object,"sceneRenderScale",.25f,1);
        if(object.contains("gameRenderScale")) result.gameRenderScale=Number(object,"gameRenderScale",.25f,1);
        result.sceneRealtime=object.value("sceneRealtime",true);result.gameRealtime=object.value("gameRealtime",true);
    }
    void ReadShaders(const Json& object,SceneRuntime::SceneSettings& result)
    {
        for(const auto& [key,target]:{std::pair{"skyShader",&result.skyShader},std::pair{"oceanShader",&result.oceanShader},std::pair{"particleShader",&result.particleShader},std::pair{"postShader",&result.postShader}})
            if(object.contains(key)) {
                const auto text=object.at(key).get<std::string>();*target=std::filesystem::path(std::u8string(text.begin(),text.end()));const auto& path=*target;
                if(!path.empty() && (path.is_absolute() || path.has_root_name() || !path.generic_u8string().starts_with(u8"Shaders/") || path.extension()!=".hlsl" ||
                    std::any_of(path.begin(),path.end(),[](const auto& part){return part=="..";}))) throw std::runtime_error("Invalid scene shader path");
            }
    }

}
namespace SceneRuntime
{
    SceneSettings ReadSceneSettings(const Json& object)
    {
        SceneSettings result;
        ReadInputs(object,result);
        ReadPhysics(object,result);
        ReadRendering(object,result);
        ReadShaders(object,result);

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
        if (object.contains("postEffects"))
        {
            const auto& effects=JsonObject(object.at("postEffects")); auto& settings=result.postEffects;
            settings.enabled=effects.at("enabled").get<bool>(); settings.bloomEnabled=effects.at("bloomEnabled").get<bool>();
            settings.exposure=Number(effects,"exposure",-10,10); settings.bloomIntensity=Number(effects,"bloomIntensity",0,10);
            settings.bloomThreshold=Number(effects,"bloomThreshold",0,100); settings.bloomRadius=Number(effects,"bloomRadius",.25f,8);
            const auto mapping=effects.at("toneMapping").get<std::string>();
            if (mapping=="none") settings.toneMapping=Engine::ToneMapping::None;
            else if (mapping=="reinhard") settings.toneMapping=Engine::ToneMapping::Reinhard;
            else if (mapping=="filmic") settings.toneMapping=Engine::ToneMapping::Filmic;
            else throw std::runtime_error("Unknown tone mapping curve");
            if (effects.contains("autoExposure")) settings.autoExposure=effects.at("autoExposure").get<bool>();
            if (effects.contains("exposureMinimum")) settings.exposureMinimum=Number(effects,"exposureMinimum",-10,10);
            if (effects.contains("exposureMaximum")) settings.exposureMaximum=Number(effects,"exposureMaximum",-10,10);
            if (effects.contains("middleGray")) settings.middleGray=Number(effects,"middleGray",.01f,1);
            if (effects.contains("contrast")) settings.contrast=Number(effects,"contrast",0,4);
            if (effects.contains("saturation")) settings.saturation=Number(effects,"saturation",0,4);
            if (effects.contains("colorFilter")) settings.colorFilter=effects.at("colorFilter").get<std::array<float,3>>();
            if (!settings.Valid()) throw std::runtime_error("Invalid post effect settings");
        }
        return result;
    }
    Json WriteSceneSettings(const SceneSettings& settings)
    {
        Json result,fog;
        result["playerInputs"]=settings.playerInputs;
        result["gravity"]=settings.gravity;
        result["fixedStep"]=settings.fixedStep;
        result["maximumFixedSteps"]=settings.maximumFixedSteps;
        result["shadowResolution"]=settings.shadowResolution;
        result["localShadowResolution"]=settings.localShadowResolution;
        result["sceneRenderScale"]=settings.sceneRenderScale;
        result["gameRenderScale"]=settings.gameRenderScale;
        result["sceneRealtime"]=settings.sceneRealtime;
        result["gameRealtime"]=settings.gameRealtime;
        if(!settings.skyShader.empty()) result["skyShader"]=settings.skyShader;
        if(!settings.oceanShader.empty()) result["oceanShader"]=settings.oceanShader;
        if(!settings.particleShader.empty()) result["particleShader"]=settings.particleShader;
        if(!settings.postShader.empty()) result["postShader"]=settings.postShader;

        result["background"]=Array(settings.background);
        result["mainCamera"]=settings.mainCamera;
        fog["enabled"]=settings.fog.enabled;
        fog["color"]=Array(settings.fog.color);
        fog["start"]=settings.fog.start;
        fog["end"]=settings.fog.end;
        fog["strength"]=settings.fog.strength;
        result["fog"]=fog;
        const auto& post=settings.postEffects;
        if (!post.Valid()) throw std::runtime_error("Invalid post effect settings");
        result["postEffects"]={{"enabled",post.enabled},{"bloomEnabled",post.bloomEnabled},{"exposure",post.exposure},
            {"bloomIntensity",post.bloomIntensity},{"bloomThreshold",post.bloomThreshold},{"bloomRadius",post.bloomRadius},
            {"toneMapping",post.toneMapping==Engine::ToneMapping::None ? "none" : post.toneMapping==Engine::ToneMapping::Reinhard ? "reinhard" : "filmic"}};
        auto& effects=result["postEffects"];
        effects["autoExposure"]=post.autoExposure; effects["exposureMinimum"]=post.exposureMinimum;
        effects["exposureMaximum"]=post.exposureMaximum; effects["middleGray"]=post.middleGray;
        effects["contrast"]=post.contrast; effects["saturation"]=post.saturation; effects["colorFilter"]=Array(post.colorFilter);
        return result;
    }
}
