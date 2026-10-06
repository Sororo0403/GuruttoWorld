#pragma once
#include <SceneRuntime/SceneLayout.h>
#include <winrt/Windows.Data.Json.h>

namespace SceneRuntime
{
    void ReadSceneComponents(const winrt::Windows::Data::Json::JsonObject& object,
        ScenePlacement& placement, bool legacy);
    winrt::Windows::Data::Json::JsonArray WriteSceneComponents(const ScenePlacement& placement);
}
