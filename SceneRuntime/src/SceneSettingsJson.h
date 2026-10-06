#pragma once
#include <SceneRuntime/SceneLayout.h>
#include <winrt/Windows.Data.Json.h>

namespace SceneRuntime
{
    SceneSettings ReadSceneSettings(const winrt::Windows::Data::Json::JsonObject& object);
    winrt::Windows::Data::Json::JsonObject WriteSceneSettings(const SceneSettings& settings);
}
