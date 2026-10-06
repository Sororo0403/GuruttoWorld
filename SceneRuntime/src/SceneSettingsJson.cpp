#include "SceneSettingsJson.h"
#include <winrt/Windows.Foundation.Collections.h>
#include <cmath>
#include <stdexcept>

namespace
{
    using namespace winrt::Windows::Data::Json;
    template<size_t N>
    std::array<float,N> Color(const JsonObject& object, const wchar_t* key)
    {
        const auto values=object.GetNamedArray(key);
        if (values.Size()!=N) throw std::runtime_error("Scene color has wrong size");
        std::array<float,N> result{};
        for (uint32_t index=0;index<N;++index)
        {
            const auto value=values.GetNumberAt(index);
            if (!std::isfinite(value) || value<0 || value>1) throw std::runtime_error("Scene color must be within [0,1]");
            result[index]=static_cast<float>(value);
        }
        return result;
    }
    float Number(const JsonObject& object, const wchar_t* key, float low, float high)
    {
        const auto value=object.GetNamedNumber(key);
        if (!std::isfinite(value) || value<low || value>high) throw std::runtime_error("Fog property out of range");
        return static_cast<float>(value);
    }
    template<size_t N>
    JsonArray Array(const std::array<float,N>& values)
    {
        JsonArray result;
        for (const auto value : values) result.Append(JsonValue::CreateNumberValue(value));
        return result;
    }
}
namespace SceneRuntime
{
    SceneSettings ReadSceneSettings(const JsonObject& object)
    {
        SceneSettings result;
        result.background=Color<4>(object,L"background");
        result.mainCamera=winrt::to_string(object.GetNamedString(L"mainCamera"));
        if (object.HasKey(L"fog"))
        {
            const auto fog=object.GetNamedObject(L"fog");
            result.fog.enabled=fog.GetNamedBoolean(L"enabled");
            result.fog.color=Color<3>(fog,L"color");
            result.fog.start=Number(fog,L"start",0,1000000);
            result.fog.end=Number(fog,L"end",0.001f,1000000);
            result.fog.strength=Number(fog,L"strength",0,1);
            if (result.fog.end-result.fog.start<=0.00001f) throw std::runtime_error("Fog end must exceed start");
        }
        return result;
    }
    JsonObject WriteSceneSettings(const SceneSettings& settings)
    {
        JsonObject result,fog;
        result.SetNamedValue(L"background",Array(settings.background));
        result.SetNamedValue(L"mainCamera",JsonValue::CreateStringValue(winrt::to_hstring(settings.mainCamera)));
        fog.SetNamedValue(L"enabled",JsonValue::CreateBooleanValue(settings.fog.enabled));
        fog.SetNamedValue(L"color",Array(settings.fog.color));
        fog.SetNamedValue(L"start",JsonValue::CreateNumberValue(settings.fog.start));
        fog.SetNamedValue(L"end",JsonValue::CreateNumberValue(settings.fog.end));
        fog.SetNamedValue(L"strength",JsonValue::CreateNumberValue(settings.fog.strength));
        result.SetNamedValue(L"fog",fog);
        return result;
    }
}
