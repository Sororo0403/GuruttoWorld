#include "SceneComponentJson.h"
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <stdexcept>
#include <unordered_set>
#undef GetObject

namespace
{
    using namespace winrt::Windows::Data::Json;
    std::filesystem::path ReadModel(const JsonObject& object)
    {
        const auto text=winrt::to_string(object.GetNamedString(L"model"));
        const auto path=std::filesystem::path(winrt::to_hstring(text).c_str());
        auto extension=path.extension().string();
        std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (path.is_absolute() || path.has_root_name() || !text.starts_with("Assets/Models/") ||
            extension!=".obj" || std::any_of(path.begin(),path.end(),[](const auto& part) { return part==L".."; }))
            throw std::runtime_error("MeshRenderer model must be an OBJ relative to Assets/Models");
        return path;
    }

    std::array<float,3> ReadVelocity(const JsonObject& object)
    {
        const auto array=object.GetNamedArray(L"angularVelocity");
        if (array.Size()!=3) throw std::runtime_error("Rotator angularVelocity requires three components");
        std::array<float,3> result;
        for (uint32_t index=0;index<3;++index)
        {
            const auto value=array.GetNumberAt(index);
            if (!std::isfinite(value) || std::abs(value)>100000.0) throw std::runtime_error("Rotator speed must be finite and within +/-100000 degrees/s");
            result[index]=static_cast<float>(value);
        }
        return result;
    }

    JsonObject Component(const std::string& id, const wchar_t* type, bool enabled)
    {
        JsonObject object;
        object.SetNamedValue(L"id",JsonValue::CreateStringValue(winrt::to_hstring(id)));
        object.SetNamedValue(L"type",JsonValue::CreateStringValue(type));
        object.SetNamedValue(L"enabled",JsonValue::CreateBooleanValue(enabled));
        return object;
    }
}

namespace SceneRuntime
{
    void ReadSceneComponents(const JsonObject& object, ScenePlacement& placement, bool legacy)
    {
        if (legacy)
        {
            if (object.HasKey(L"components")) throw std::runtime_error("Version 2 cannot contain components");
            placement.SetModel(ReadModel(object));
            return;
        }
        if (object.HasKey(L"model")) throw std::runtime_error("Version 3 model belongs to MeshRenderer");
        std::unordered_set<std::string> ids{"transform"};
        for (const auto& value : object.GetNamedArray(L"components"))
        {
            const auto component=value.GetObject();
            const auto id=winrt::to_string(component.GetNamedString(L"id"));
            if (id.empty() || id.find('\0')!=std::string::npos || !ids.insert(id).second)
                throw std::runtime_error("Empty, duplicate or reserved component ID");
            const auto type=component.GetNamedString(L"type");
            const bool enabled=component.GetNamedBoolean(L"enabled");
            if (type==L"MeshRenderer")
            {
                if (placement.meshRenderer) throw std::runtime_error("Only one MeshRenderer is allowed");
                placement.meshRenderer=MeshRendererComponent{id,enabled,ReadModel(component)};
            }
            else if (type==L"Rotator")
            {
                if (placement.rotator) throw std::runtime_error("Only one Rotator is allowed");
                placement.rotator=RotatorComponent{id,enabled,ReadVelocity(component)};
            }
            else throw std::runtime_error("Unsupported component type: "+winrt::to_string(type));
        }
    }

    JsonArray WriteSceneComponents(const ScenePlacement& placement)
    {
        JsonArray components;
        if (placement.meshRenderer)
        {
            const auto& mesh=*placement.meshRenderer;
            auto object=Component(mesh.id,L"MeshRenderer",mesh.enabled);
            const auto text=mesh.model.generic_u8string();
            object.SetNamedValue(L"model",JsonValue::CreateStringValue(winrt::to_hstring(
                std::string_view(reinterpret_cast<const char*>(text.data()),text.size()))));
            components.Append(object);
        }
        if (placement.rotator)
        {
            const auto& rotator=*placement.rotator;
            auto object=Component(rotator.id,L"Rotator",rotator.enabled);
            JsonArray velocity;
            for (const auto value : rotator.angularVelocity)
            {
                if (!std::isfinite(value) || std::abs(value)>100000.0f) throw std::runtime_error("Invalid Rotator speed");
                velocity.Append(JsonValue::CreateNumberValue(value));
            }
            object.SetNamedValue(L"angularVelocity",velocity);
            components.Append(object);
        }
        return components;
    }
}
