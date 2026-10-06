#pragma once
#include <SceneRuntime/SceneLayout.h>
#include <winrt/Windows.Data.Json.h>
namespace SceneRuntime {
bool ReadUiComponent(const winrt::Windows::Data::Json::JsonObject&, ScenePlacement&, const std::string&);
void WriteUiComponents(winrt::Windows::Data::Json::JsonArray&, const ScenePlacement&);
}
