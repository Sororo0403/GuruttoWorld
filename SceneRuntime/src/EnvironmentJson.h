#pragma once
#include <SceneRuntime/SceneLayout.h>
#include <winrt/Windows.Data.Json.h>

namespace SceneRuntime
{
    bool ReadEnvironmentComponent(const winrt::Windows::Data::Json::JsonObject& object,
        ScenePlacement& placement, const std::string& type);
    void WriteEnvironmentComponents(winrt::Windows::Data::Json::JsonArray& array, const ScenePlacement& placement);
}
