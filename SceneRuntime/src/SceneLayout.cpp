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
#include <atomic>
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
                (scale && std::abs(value) < static_cast<double>(0.000001f)))
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

    std::string SceneLayout::Serialize() const
    {
        JsonApartment apartment;
        using namespace winrt::Windows::Data::Json;
        const auto vector = [](const std::array<float, 3>& values)
        {
            JsonArray array;
            for (float value : values)
            {
                if (!std::isfinite(value)) throw std::runtime_error("Cannot save nonfinite transform");
                array.Append(JsonValue::CreateNumberValue(value));
            }
            return array;
        };
        std::string json = "{\n  \"version\": 1,\n  \"objects\": [\n";
        for (size_t i = 0; i < objects.size(); ++i)
        {
            const auto& placement = objects[i];
            JsonObject object;
            object.SetNamedValue(L"id", JsonValue::CreateStringValue(winrt::to_hstring(placement.id)));
            object.SetNamedValue(L"name", JsonValue::CreateStringValue(winrt::to_hstring(placement.name)));
            const auto model = placement.model.generic_u8string();
            object.SetNamedValue(L"model", JsonValue::CreateStringValue(winrt::to_hstring(
                std::string_view(reinterpret_cast<const char*>(model.data()), model.size()))));
            object.SetNamedValue(L"position", vector(placement.position));
            object.SetNamedValue(L"rotation", vector(placement.rotation));
            object.SetNamedValue(L"scale", vector(placement.scale));
            json += "    " + winrt::to_string(object.Stringify()) + (i + 1 == objects.size() ? "\n" : ",\n");
        }
        json += "  ]\n}\n";
        static_cast<void>(Parse(json)); // 再読み込みできるデータだけを書き出します。
        return json;
    }

    void SceneLayout::Save(const std::filesystem::path& path) const
    {
        const auto json = Serialize();
        static std::atomic<unsigned long long> sequence{ 0 };
        auto temporary = path;
        temporary += L"." + std::to_wstring(GetCurrentProcessId()) + L"." +
            std::to_wstring(GetTickCount64()) + L"." + std::to_wstring(sequence++) + L".tmp";
        HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create save file (error " +
            std::to_string(GetLastError()) + ")");
        try
        {
            size_t offset = 0;
            while (offset < json.size())
            {
                const auto count = static_cast<DWORD>((std::min)(json.size() - offset, size_t(1024 * 1024)));
                DWORD written = 0;
                if (!WriteFile(file, json.data() + offset, count, &written, nullptr) || !written)
                    throw std::runtime_error("Cannot write save file");
                offset += written;
            }
            if (!FlushFileBuffers(file)) throw std::runtime_error("Cannot flush save file");
            CloseHandle(file);
            file = INVALID_HANDLE_VALUE;
            if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("Cannot replace layout file (error " + std::to_string(GetLastError()) + ")");
        }
        catch (...)
        {
            if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
            DeleteFileW(temporary.c_str());
            throw;
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
