#include <SceneRuntime/SceneLayout.h>
#include "SceneComponentJson.h"
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <roapi.h>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <unordered_map>
#include <optional>
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

    SceneRuntime::SceneSettings ReadSettings(const winrt::Windows::Data::Json::JsonObject& object)
    {
        SceneRuntime::SceneSettings result;
        const auto background=object.GetNamedArray(L"background");
        if (background.Size()!=4) throw std::runtime_error("Background requires four components");
        for (uint32_t index=0;index<4;++index)
        {
            const auto value=background.GetNumberAt(index);
            if (!std::isfinite(value) || value<0 || value>1) throw std::runtime_error("Background must be within [0,1]");
            result.background[index]=static_cast<float>(value);
        }
        result.mainCamera=winrt::to_string(object.GetNamedString(L"mainCamera"));
        return result;
    }

    void ValidateCamera(const SceneRuntime::SceneLayout& layout)
    {
        if (layout.settings.mainCamera.empty()) return;
        if (std::none_of(layout.objects.begin(),layout.objects.end(),[&](const auto& object) {
            return object.id==layout.settings.mainCamera && object.camera;
        })) throw std::runtime_error("Main camera must reference an object with Camera");
    }

    void ValidateParents(const std::vector<SceneRuntime::ScenePlacement>& objects)
    {
        std::unordered_map<std::string,size_t> indices;
        for (size_t index=0;index<objects.size();++index) indices.emplace(objects[index].id,index);
        std::vector<std::optional<size_t>> parents(objects.size());
        for (size_t index=0;index<objects.size();++index)
        {
            const auto& object=objects[index];
            if (object.parentId.empty()) continue;
            const auto found=indices.find(object.parentId);
            if (found==indices.end()) throw std::runtime_error(object.id+": missing parent "+object.parentId);
            if (found->second==index) throw std::runtime_error(object.id+": object cannot parent itself");
            parents[index]=found->second;
        }
        std::vector<unsigned char> visited(objects.size(),0);
        for (size_t index=0;index<objects.size();++index)
        {
            std::vector<size_t> chain;
            std::optional<size_t> current=index;
            while (current && visited[*current]==0)
            {
                visited[*current]=1;
                chain.push_back(*current);
                current=parents[*current];
            }
            if (current && visited[*current]==1) throw std::runtime_error(objects[*current].id+": parent cycle detected");
            for (const auto item : chain) visited[item]=2;
        }
    }

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
            const double version=document.GetNamedNumber(L"version");
            if (version!=2.0 && version!=3.0 && version!=4.0)
                throw std::runtime_error("Unsupported layout version");
            SceneLayout layout;
            if (version==4.0) layout.settings=ReadSettings(document.GetNamedObject(L"settings"));
            if (document.HasKey(L"transformSpace")) throw std::runtime_error("Scenes always use local transforms");
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
                    if (object.HasKey(L"parent")) placement.parentId = winrt::to_string(object.GetNamedString(L"parent"));
                    ReadSceneComponents(object,placement,version==2.0);
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
            ValidateParents(layout.objects);
            ValidateCamera(layout);
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
        JsonObject settingsObject;
        JsonArray background;
        for (const auto value : settings.background) background.Append(JsonValue::CreateNumberValue(value));
        settingsObject.SetNamedValue(L"background",background);
        settingsObject.SetNamedValue(L"mainCamera",JsonValue::CreateStringValue(winrt::to_hstring(settings.mainCamera)));
        std::string json = "{\n  \"version\": 4,\n  \"settings\": "+winrt::to_string(settingsObject.Stringify())+",\n  \"objects\": [\n";
        for (size_t i = 0; i < objects.size(); ++i)
        {
            const auto& placement = objects[i];
            JsonObject object;
            object.SetNamedValue(L"id", JsonValue::CreateStringValue(winrt::to_hstring(placement.id)));
            object.SetNamedValue(L"name", JsonValue::CreateStringValue(winrt::to_hstring(placement.name)));
            if (!placement.parentId.empty())
                object.SetNamedValue(L"parent",JsonValue::CreateStringValue(winrt::to_hstring(placement.parentId)));
            object.SetNamedValue(L"components",WriteSceneComponents(placement));
            object.SetNamedValue(L"position", vector(placement.position));
            object.SetNamedValue(L"rotation", vector(placement.rotation));
            object.SetNamedValue(L"scale", vector(placement.scale));
            json += "    " + winrt::to_string(object.Stringify()) + (i + 1 == objects.size() ? "\n" : ",\n");
        }
        json += "  ]\n}\n";
        static_cast<void>(Parse(json)); // 再読み込みできるデータだけを書き出します。
        return json;
    }

    void SceneLayout::Save(const std::filesystem::path& path, bool overwrite) const
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
            if (!MoveFileExW(temporary.c_str(), path.c_str(), (overwrite ? MOVEFILE_REPLACE_EXISTING : 0) | MOVEFILE_WRITE_THROUGH))
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
