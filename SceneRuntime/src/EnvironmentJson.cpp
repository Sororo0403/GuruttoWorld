#include "EnvironmentJson.h"
#include <cmath>
#include <stdexcept>

namespace
{
    using Engine::Json;
    using Engine::JsonNumber;
    using Engine::JsonArray;
    using Engine::JsonObject;
    float Number(const Json& object, const char* key, float low, float high)
    {
        const auto value=JsonNumber(object.at(key));
        if (!std::isfinite(value) || value<low || value>high)
            throw std::runtime_error("Environment property out of range: "+std::string(key));
        return static_cast<float>(value);
    }
    template<size_t N>
    std::array<float,N> Vector(const Json& object, const char* key, float low, float high)
    {
        const auto& values=JsonArray(object.at(key));
        if (values.size()!=N) throw std::runtime_error("Environment vector has wrong size");
        std::array<float,N> result{};
        for (uint32_t index=0;index<N;++index)
        {
            const auto value=JsonNumber(values.at(index));
            if (!std::isfinite(value) || value<low || value>high) throw std::runtime_error("Environment vector out of range");
            result[index]=static_cast<float>(value);
        }
        return result;
    }
    void Put(Json& object, const char* key, float value) { object[key]=value; }
    template<size_t N>
    void Put(Json& object, const char* key, const std::array<float,N>& values)
    {
        object[key]=values;
    }
    template<class T>
    T ReadBase(const Json& object, const std::optional<T>& existing)
    {
        if (existing) throw std::runtime_error("Only one component of each type is allowed");
        T result;
        result.id=object.at("id").get<std::string>();
        result.enabled=object.at("enabled").get<bool>();
        return result;
    }
    template<class T>
    Json WriteBase(const T& component, const char* type)
    {
        Json object;
        object["id"]=component.id;
        object["type"]=type;
        object["enabled"]=component.enabled;
        return object;
    }
    void ReadCamera(const Json& object, SceneRuntime::ScenePlacement& placement)
    {
        auto camera=ReadBase(object,placement.camera);
        camera.verticalFov=Number(object,"verticalFov",1,175);
        camera.nearClip=Number(object,"nearClip",0.001f,100000);
        camera.farClip=Number(object,"farClip",0.002f,1000000);
        if (camera.farClip-camera.nearClip<=0.00001f) throw std::runtime_error("Camera far clip must exceed near clip");
        camera.referenceAspect=Number(object,"referenceAspect",0.01f,100);
        camera.preserveHorizontal=object.at("preserveHorizontal").get<bool>();
        placement.camera=std::move(camera);
    }
    void ReadLight(const Json& object, SceneRuntime::ScenePlacement& placement)
    {
        auto light=ReadBase(object,placement.directionalLight);
        light.direction=Vector<3>(object,"direction",-100000,100000);
        light.color=Vector<3>(object,"color",0,1);
        light.intensity=Number(object,"intensity",0,100);
        light.ambient=Number(object,"ambient",0,100);
        light.specular=Number(object,"specular",0,100);
        light.shininess=Number(object,"shininess",1,10000);
        if (object.contains("shadowsEnabled")) light.shadowsEnabled=object.at("shadowsEnabled").get<bool>();
        if (object.contains("shadowDistance")) light.shadowDistance=Number(object,"shadowDistance",10,200);
        if (object.contains("shadowBias")) light.shadowBias=Number(object,"shadowBias",0,.01f);
        placement.directionalLight=std::move(light);
    }
    void ReadSky(const Json& object, SceneRuntime::ScenePlacement& placement)
    {
        auto sky=ReadBase(object,placement.sky);
        sky.horizon=Vector<3>(object,"horizon",0,1); sky.zenith=Vector<3>(object,"zenith",0,1);
        sky.horizonHeight=Number(object,"horizonHeight",0.001f,2);
        sky.sunColor=Vector<3>(object,"sunColor",0,1);
        sky.sunCenter=Vector<2>(object,"sunCenter",-10,10); sky.sunRadius=Vector<2>(object,"sunRadius",0.001f,10);
        sky.sunStrength=Number(object,"sunStrength",0,1);
        sky.cloudLow=Vector<3>(object,"cloudLow",0,1); sky.cloudHigh=Vector<3>(object,"cloudHigh",0,1);
        sky.cloudOpacity=Number(object,"cloudOpacity",0,1);
        sky.cloudVelocity=Vector<2>(object,"cloudVelocity",-10,10);
        sky.referenceAspect=Number(object,"referenceAspect",0.01f,100);
        const auto clouds=JsonArray(object.at("clouds"));
        if (clouds.size()!=3) throw std::runtime_error("Sky requires three cloud banks");
        for (uint32_t index=0;index<3;++index)
        {
            const auto& cloud=JsonObject(clouds.at(index));
            sky.clouds[index].center=Vector<2>(cloud,"center",-10,10);
            sky.clouds[index].size=Number(cloud,"size",0,10);
        }
        placement.sky=std::move(sky);
    }
    void ReadEmitter(const Json& object, SceneRuntime::ScenePlacement& placement)
    {
        auto emitter=ReadBase(object,placement.particleEmitter);
        const auto count=JsonNumber(object.at("count"));
        if (!std::isfinite(count) || count<0 || count>1024 || std::floor(count)!=count)
            throw std::runtime_error("Particle count must be an integer within [0,1024]");
        emitter.count=static_cast<unsigned int>(count);
        emitter.color=Vector<4>(object,"color",0,1);
        emitter.size=Number(object,"size",0.001f,1000);
        emitter.extent=Vector<3>(object,"extent",0,100000);
        emitter.travel=Vector<3>(object,"travel",-100000,100000);
        emitter.cycle=Number(object,"cycle",0.01f,100000);
        emitter.drift=Number(object,"drift",0,100000);
        placement.particleEmitter=std::move(emitter);
    }
    void ReadSway(const Json& object, SceneRuntime::ScenePlacement& placement)
    {
        auto sway=ReadBase(object,placement.cameraSway);
        sway.amplitude=Vector<3>(object,"amplitude",-100000,100000);
        sway.period=Vector<3>(object,"period",0.01f,100000);
        placement.cameraSway=std::move(sway);
    }
    void WriteSky(Json& array, const SceneRuntime::SkyComponent& sky)
    {
        auto object=WriteBase(sky,"Sky");
        Put(object,"horizon",sky.horizon); Put(object,"zenith",sky.zenith); Put(object,"horizonHeight",sky.horizonHeight);
        Put(object,"sunColor",sky.sunColor); Put(object,"sunCenter",sky.sunCenter); Put(object,"sunRadius",sky.sunRadius);
        Put(object,"sunStrength",sky.sunStrength); Put(object,"cloudLow",sky.cloudLow); Put(object,"cloudHigh",sky.cloudHigh);
        Put(object,"cloudOpacity",sky.cloudOpacity); Put(object,"cloudVelocity",sky.cloudVelocity); Put(object,"referenceAspect",sky.referenceAspect);
        Json clouds=Json::array();
        for (const auto& cloud : sky.clouds)
        {
            Json bank; Put(bank,"center",cloud.center); Put(bank,"size",cloud.size); clouds.push_back(bank);
        }
        object["clouds"]=clouds; array.push_back(object);
    }
    void WriteEmitter(Json& array, const SceneRuntime::ParticleEmitterComponent& emitter)
    {
        auto object=WriteBase(emitter,"ParticleEmitter");
        Put(object,"count",static_cast<float>(emitter.count)); Put(object,"color",emitter.color);
        Put(object,"size",emitter.size); Put(object,"extent",emitter.extent); Put(object,"travel",emitter.travel);
        Put(object,"cycle",emitter.cycle); Put(object,"drift",emitter.drift); array.push_back(object);
    }
    void WriteSway(Json& array, const SceneRuntime::CameraSwayComponent& sway)
    {
        auto object=WriteBase(sway,"CameraSway");
        Put(object,"amplitude",sway.amplitude); Put(object,"period",sway.period); array.push_back(object);
    }
    void WriteCamera(Json& array, const SceneRuntime::CameraComponent& camera)
    {
        auto object=WriteBase(camera,"Camera");
        Put(object,"verticalFov",camera.verticalFov); Put(object,"nearClip",camera.nearClip);
        Put(object,"farClip",camera.farClip); Put(object,"referenceAspect",camera.referenceAspect);
        object["preserveHorizontal"]=camera.preserveHorizontal;
        array.push_back(object);
    }
    void WriteLight(Json& array, const SceneRuntime::DirectionalLightComponent& light)
    {
        auto object=WriteBase(light,"DirectionalLight");
        Put(object,"direction",light.direction); Put(object,"color",light.color);
        Put(object,"intensity",light.intensity); Put(object,"ambient",light.ambient);
        Put(object,"specular",light.specular); Put(object,"shininess",light.shininess);
        object["shadowsEnabled"]=light.shadowsEnabled; Put(object,"shadowDistance",light.shadowDistance); Put(object,"shadowBias",light.shadowBias);
        array.push_back(object);
    }
}
namespace SceneRuntime
{
    bool ReadEnvironmentComponent(const Json& object, ScenePlacement& placement, const std::string& type)
    {
        if (type=="Camera") ReadCamera(object,placement);
        else if (type=="DirectionalLight") ReadLight(object,placement);
        else if (type=="Sky") ReadSky(object,placement);
        else if (type=="ParticleEmitter") ReadEmitter(object,placement);
        else if (type=="CameraSway") ReadSway(object,placement);
        else return false;
        return true;
    }
    void WriteEnvironmentComponents(Json& array, const ScenePlacement& placement)
    {
        if (placement.camera) WriteCamera(array,*placement.camera);
        if (placement.directionalLight) WriteLight(array,*placement.directionalLight);
        if (placement.sky) WriteSky(array,*placement.sky);
        if (placement.particleEmitter) WriteEmitter(array,*placement.particleEmitter);
        if (placement.cameraSway) WriteSway(array,*placement.cameraSway);
    }
}
