#include "EnvironmentJson.h"
#include <winrt/Windows.Foundation.Collections.h>
#include <cmath>
#include <stdexcept>
#undef GetObject

namespace
{
    using namespace winrt::Windows::Data::Json;
    float Number(const JsonObject& object, const wchar_t* key, float low, float high)
    {
        const auto value=object.GetNamedNumber(key);
        if (!std::isfinite(value) || value<low || value>high)
            throw std::runtime_error("Environment property out of range: "+winrt::to_string(key));
        return static_cast<float>(value);
    }
    template<size_t N>
    std::array<float,N> Vector(const JsonObject& object, const wchar_t* key, float low, float high)
    {
        const auto values=object.GetNamedArray(key);
        if (values.Size()!=N) throw std::runtime_error("Environment vector has wrong size");
        std::array<float,N> result{};
        for (uint32_t index=0;index<N;++index)
        {
            const auto value=values.GetNumberAt(index);
            if (!std::isfinite(value) || value<low || value>high) throw std::runtime_error("Environment vector out of range");
            result[index]=static_cast<float>(value);
        }
        return result;
    }
    void Put(JsonObject& object, const wchar_t* key, float value) { object.SetNamedValue(key,JsonValue::CreateNumberValue(value)); }
    template<size_t N>
    void Put(JsonObject& object, const wchar_t* key, const std::array<float,N>& values)
    {
        JsonArray array;
        for (const auto value : values) array.Append(JsonValue::CreateNumberValue(value));
        object.SetNamedValue(key,array);
    }
    template<class T>
    T ReadBase(const JsonObject& object, const std::optional<T>& existing)
    {
        if (existing) throw std::runtime_error("Only one component of each type is allowed");
        T result;
        result.id=winrt::to_string(object.GetNamedString(L"id"));
        result.enabled=object.GetNamedBoolean(L"enabled");
        return result;
    }
    template<class T>
    JsonObject WriteBase(const T& component, const wchar_t* type)
    {
        JsonObject object;
        object.SetNamedValue(L"id",JsonValue::CreateStringValue(winrt::to_hstring(component.id)));
        object.SetNamedValue(L"type",JsonValue::CreateStringValue(type));
        object.SetNamedValue(L"enabled",JsonValue::CreateBooleanValue(component.enabled));
        return object;
    }
    void ReadCamera(const JsonObject& object, SceneRuntime::ScenePlacement& placement)
    {
        auto camera=ReadBase(object,placement.camera);
        camera.verticalFov=Number(object,L"verticalFov",1,175);
        camera.nearClip=Number(object,L"nearClip",0.001f,100000);
        camera.farClip=Number(object,L"farClip",0.002f,1000000);
        if (camera.farClip-camera.nearClip<=0.00001f) throw std::runtime_error("Camera far clip must exceed near clip");
        camera.referenceAspect=Number(object,L"referenceAspect",0.01f,100);
        camera.preserveHorizontal=object.GetNamedBoolean(L"preserveHorizontal");
        placement.camera=std::move(camera);
    }
    void ReadLight(const JsonObject& object, SceneRuntime::ScenePlacement& placement)
    {
        auto light=ReadBase(object,placement.directionalLight);
        light.direction=Vector<3>(object,L"direction",-100000,100000);
        light.color=Vector<3>(object,L"color",0,1);
        light.intensity=Number(object,L"intensity",0,100);
        light.ambient=Number(object,L"ambient",0,100);
        light.specular=Number(object,L"specular",0,100);
        light.shininess=Number(object,L"shininess",1,10000);
        placement.directionalLight=std::move(light);
    }
    void ReadSky(const JsonObject& object, SceneRuntime::ScenePlacement& placement)
    {
        auto sky=ReadBase(object,placement.sky);
        sky.horizon=Vector<3>(object,L"horizon",0,1); sky.zenith=Vector<3>(object,L"zenith",0,1);
        sky.horizonHeight=Number(object,L"horizonHeight",0.001f,2);
        sky.sunColor=Vector<3>(object,L"sunColor",0,1);
        sky.sunCenter=Vector<2>(object,L"sunCenter",-10,10); sky.sunRadius=Vector<2>(object,L"sunRadius",0.001f,10);
        sky.sunStrength=Number(object,L"sunStrength",0,1);
        sky.cloudLow=Vector<3>(object,L"cloudLow",0,1); sky.cloudHigh=Vector<3>(object,L"cloudHigh",0,1);
        sky.cloudOpacity=Number(object,L"cloudOpacity",0,1);
        sky.cloudVelocity=Vector<2>(object,L"cloudVelocity",-10,10);
        sky.referenceAspect=Number(object,L"referenceAspect",0.01f,100);
        const auto clouds=object.GetNamedArray(L"clouds");
        if (clouds.Size()!=3) throw std::runtime_error("Sky requires three cloud banks");
        for (uint32_t index=0;index<3;++index)
        {
            const auto cloud=clouds.GetObjectAt(index);
            sky.clouds[index].center=Vector<2>(cloud,L"center",-10,10);
            sky.clouds[index].size=Number(cloud,L"size",0,10);
        }
        placement.sky=std::move(sky);
    }
    void ReadEmitter(const JsonObject& object, SceneRuntime::ScenePlacement& placement)
    {
        auto emitter=ReadBase(object,placement.particleEmitter);
        const auto count=object.GetNamedNumber(L"count");
        if (!std::isfinite(count) || count<0 || count>1024 || std::floor(count)!=count)
            throw std::runtime_error("Particle count must be an integer within [0,1024]");
        emitter.count=static_cast<unsigned int>(count);
        emitter.color=Vector<4>(object,L"color",0,1);
        emitter.size=Number(object,L"size",0.001f,1000);
        emitter.extent=Vector<3>(object,L"extent",0,100000);
        emitter.travel=Vector<3>(object,L"travel",-100000,100000);
        emitter.cycle=Number(object,L"cycle",0.01f,100000);
        emitter.drift=Number(object,L"drift",0,100000);
        placement.particleEmitter=std::move(emitter);
    }
    void ReadSway(const JsonObject& object, SceneRuntime::ScenePlacement& placement)
    {
        auto sway=ReadBase(object,placement.cameraSway);
        sway.amplitude=Vector<3>(object,L"amplitude",-100000,100000);
        sway.period=Vector<3>(object,L"period",0.01f,100000);
        placement.cameraSway=std::move(sway);
    }
    void WriteSky(JsonArray& array, const SceneRuntime::SkyComponent& sky)
    {
        auto object=WriteBase(sky,L"Sky");
        Put(object,L"horizon",sky.horizon); Put(object,L"zenith",sky.zenith); Put(object,L"horizonHeight",sky.horizonHeight);
        Put(object,L"sunColor",sky.sunColor); Put(object,L"sunCenter",sky.sunCenter); Put(object,L"sunRadius",sky.sunRadius);
        Put(object,L"sunStrength",sky.sunStrength); Put(object,L"cloudLow",sky.cloudLow); Put(object,L"cloudHigh",sky.cloudHigh);
        Put(object,L"cloudOpacity",sky.cloudOpacity); Put(object,L"cloudVelocity",sky.cloudVelocity); Put(object,L"referenceAspect",sky.referenceAspect);
        JsonArray clouds;
        for (const auto& cloud : sky.clouds)
        {
            JsonObject bank; Put(bank,L"center",cloud.center); Put(bank,L"size",cloud.size); clouds.Append(bank);
        }
        object.SetNamedValue(L"clouds",clouds); array.Append(object);
    }
    void WriteEmitter(JsonArray& array, const SceneRuntime::ParticleEmitterComponent& emitter)
    {
        auto object=WriteBase(emitter,L"ParticleEmitter");
        Put(object,L"count",static_cast<float>(emitter.count)); Put(object,L"color",emitter.color);
        Put(object,L"size",emitter.size); Put(object,L"extent",emitter.extent); Put(object,L"travel",emitter.travel);
        Put(object,L"cycle",emitter.cycle); Put(object,L"drift",emitter.drift); array.Append(object);
    }
    void WriteSway(JsonArray& array, const SceneRuntime::CameraSwayComponent& sway)
    {
        auto object=WriteBase(sway,L"CameraSway");
        Put(object,L"amplitude",sway.amplitude); Put(object,L"period",sway.period); array.Append(object);
    }
    void WriteCamera(JsonArray& array, const SceneRuntime::CameraComponent& camera)
    {
        auto object=WriteBase(camera,L"Camera");
        Put(object,L"verticalFov",camera.verticalFov); Put(object,L"nearClip",camera.nearClip);
        Put(object,L"farClip",camera.farClip); Put(object,L"referenceAspect",camera.referenceAspect);
        object.SetNamedValue(L"preserveHorizontal",JsonValue::CreateBooleanValue(camera.preserveHorizontal));
        array.Append(object);
    }
    void WriteLight(JsonArray& array, const SceneRuntime::DirectionalLightComponent& light)
    {
        auto object=WriteBase(light,L"DirectionalLight");
        Put(object,L"direction",light.direction); Put(object,L"color",light.color);
        Put(object,L"intensity",light.intensity); Put(object,L"ambient",light.ambient);
        Put(object,L"specular",light.specular); Put(object,L"shininess",light.shininess);
        array.Append(object);
    }
}
namespace SceneRuntime
{
    bool ReadEnvironmentComponent(const JsonObject& object, ScenePlacement& placement, const std::string& type)
    {
        if (type=="Camera") ReadCamera(object,placement);
        else if (type=="DirectionalLight") ReadLight(object,placement);
        else if (type=="Sky") ReadSky(object,placement);
        else if (type=="ParticleEmitter") ReadEmitter(object,placement);
        else if (type=="CameraSway") ReadSway(object,placement);
        else return false;
        return true;
    }
    void WriteEnvironmentComponents(JsonArray& array, const ScenePlacement& placement)
    {
        if (placement.camera) WriteCamera(array,*placement.camera);
        if (placement.directionalLight) WriteLight(array,*placement.directionalLight);
        if (placement.sky) WriteSky(array,*placement.sky);
        if (placement.particleEmitter) WriteEmitter(array,*placement.particleEmitter);
        if (placement.cameraSway) WriteSway(array,*placement.cameraSway);
    }
}
