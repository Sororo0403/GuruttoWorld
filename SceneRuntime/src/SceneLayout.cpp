#include <SceneRuntime/SceneLayout.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <roapi.h>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#undef GetObject
#pragma comment(lib, "windowsapp.lib")

namespace
{
    // アプリの既存COM初期化方式を変えず、単体テストからも利用できます。
    struct JsonApartment
    {
        HRESULT result = RoInitialize(RO_INIT_MULTITHREADED);
        JsonApartment()
        {
            if (FAILED(result) && result != RPC_E_CHANGED_MODE) winrt::check_hresult(result);
        }
        ~JsonApartment() { if (SUCCEEDED(result)) RoUninitialize(); }
    };

    std::array<float, 3> ReadVector(const winrt::Windows::Data::Json::JsonObject& object,
        const wchar_t* key, bool scale = false)
    {
        const auto values = object.GetNamedArray(key);
        if (values.Size() != 3) throw std::runtime_error("Transform requires three components");
        std::array<float, 3> result;
        for (uint32_t i = 0; i < 3; ++i)
        {
            const double value = values.GetNumberAt(i);
            if (!std::isfinite(value) || std::abs(value) > (std::numeric_limits<float>::max)() ||
                (scale && std::abs(value) < 0.000001))
                throw std::runtime_error("Transform contains an invalid number or zero scale");
            result[i] = static_cast<float>(value);
        }
        return result;
    }
}

namespace SceneRuntime
{
    SceneLayout SceneLayout::Parse(std::string_view json)
    {
        try
        {
            JsonApartment apartment;
            const auto document = winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(json));
            if (document.GetNamedNumber(L"version") != 1.0)
                throw std::runtime_error("Unsupported layout version");
            SceneLayout layout;
            std::unordered_set<std::string> ids;
            for (const auto& value : document.GetNamedArray(L"objects"))
            {
                const auto object = value.GetObject();
                ScenePlacement placement;
                placement.id = winrt::to_string(object.GetNamedString(L"id"));
                if (placement.id.empty() || !ids.insert(placement.id).second)
                    throw std::runtime_error("Empty or duplicate object ID: " + placement.id);
                try
                {
                    placement.name = winrt::to_string(object.GetNamedString(L"name"));
                    const auto model = winrt::to_string(object.GetNamedString(L"model"));
                    placement.model = std::filesystem::path(winrt::to_hstring(model).c_str());
                    if (placement.model.is_absolute() || placement.model.has_root_name() ||
                        !model.starts_with("Assets/Models/Title/") || placement.model.extension() != L".obj")
                        throw std::runtime_error("Model must be an OBJ relative to Assets/Models/Title");
                    for (const auto& part : placement.model)
                        if (part == L"..") throw std::runtime_error("Model path cannot contain parent traversal");
                    placement.position = ReadVector(object, L"position");
                    placement.rotation = ReadVector(object, L"rotation");
                    placement.scale = ReadVector(object, L"scale", true);
                }
                catch (const winrt::hresult_error& error)
                {
                    throw std::runtime_error(placement.id + ": " + winrt::to_string(error.message()));
                }
                catch (const std::exception& error)
                {
                    throw std::runtime_error(placement.id + ": " + error.what());
                }
                layout.objects.push_back(std::move(placement));
            }
            return layout;
        }
        catch (const winrt::hresult_error& error)
        {
            throw std::runtime_error("Invalid layout JSON: " + winrt::to_string(error.message()));
        }
    }

    SceneLayout SceneLayout::Load(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) throw std::runtime_error("Cannot open layout: " + path.string());
        std::string json{ std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
        if (stream.bad()) throw std::runtime_error("Cannot read layout: " + path.string());
        // 一般的なテキストエディターが付けるUTF-8 BOMを許可します。
        if (json.starts_with("\xEF\xBB\xBF")) json.erase(0, 3);
        return Parse(json);
    }
}
