#include <SceneRuntime/SceneLayout.h>
#include "SceneComponentJson.h"
#include "SceneSettingsJson.h"
#include <SceneRuntime/Prefab.h>
#include <Engine/Core/Json.h>
#include <Windows.h>
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

namespace
{
    using Engine::Json;
    using Engine::JsonNumber;
    using Engine::JsonArray;
    using Engine::JsonObject;

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

    std::array<float, 3> ReadVector(const Json& object,
        const char* key, bool scale = false)
    {
        const auto& values = JsonArray(object.at(key));
        if (values.size() != 3) throw std::runtime_error("Transform requires three components");
        std::array<float, 3> result;
        for (uint32_t i = 0; i < 3; ++i)
        {
            const double value = JsonNumber(values.at(i));
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
            const auto document = Json::parse(json);
            JsonObject(document);
            const double version=JsonNumber(document.at("version"));
            if (version!=2.0 && version!=3.0 && version!=4.0)
                throw std::runtime_error("Unsupported layout version");
            SceneLayout layout;
            if (version==4.0) layout.settings=ReadSceneSettings(JsonObject(document.at("settings")));
            if (document.contains("transformSpace")) throw std::runtime_error("Scenes always use local transforms");
            std::unordered_set<std::string> ids;
            for (const auto& value : JsonArray(document.at("objects")))
            {
                const auto& object = JsonObject(value);
                ScenePlacement placement;
                placement.id = object.at("id").get<std::string>();
                if (placement.id.empty() || !ids.insert(placement.id).second)
                    throw std::runtime_error("Empty or duplicate object ID: " + placement.id);
                try
                {
                    placement.name = object.at("name").get<std::string>();
                    if (object.contains("parent")) placement.parentId = object.at("parent").get<std::string>();
                    ReadSceneComponents(object,placement,version==2.0);
                    if (object.contains("prefab"))
                    {
                        const auto& prefabValue=JsonObject(object.at("prefab"));
                        const auto asset=prefabValue.at("asset").get<std::string>();
                        PrefabLink link; link.asset=std::filesystem::path(std::u8string(asset.begin(),asset.end()));
                        link.sourceId=prefabValue.at("source").get<std::string>(); link.rootId=prefabValue.at("root").get<std::string>();
                        link.baseline=prefabValue.at("baseline").get<std::string>();
                        if (!Prefab::ValidPath(link.asset) || link.sourceId.empty() || link.rootId.empty() || link.baseline.empty() || link.baseline.size()>1048576)
                            throw std::runtime_error("Invalid prefab link");
                        placement.prefab=std::move(link);
                    }
                    placement.position = ReadVector(object, "position");
                    placement.rotation = ReadVector(object, "rotation");
                    placement.scale = ReadVector(object, "scale", true);
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
        catch (const Json::exception& error)
        {
            throw std::runtime_error("Invalid layout JSON: " + std::string(error.what()));
        }
    }

    std::string SceneLayout::Serialize() const
    {
        const auto settingsObject=WriteSceneSettings(settings);
        std::string json="{\n  \"version\": 4,\n  \"settings\": "+settingsObject.dump()+",\n  \"objects\": [\n";
        for(size_t i=0;i<objects.size();++i) {
            const auto& p=objects[i];
            Json object={{"id",p.id},{"name",p.name},{"parent",p.parentId},
                {"components",WriteSceneComponents(p)},{"position",p.position},{"rotation",p.rotation},{"scale",p.scale}};
            if (p.prefab) object["prefab"]={{"asset",p.prefab->asset},{"source",p.prefab->sourceId},{"root",p.prefab->rootId},{"baseline",p.prefab->baseline}};
            for(const auto& vector:{p.position,p.rotation,p.scale})
                if(std::any_of(vector.begin(),vector.end(),[](float value){return !std::isfinite(value);}))
                    throw std::runtime_error("Cannot save nonfinite transform");
            json+="    "+object.dump()+(i+1==objects.size()?"\n":",\n");
        }
        json+="  ]\n}\n";
        static_cast<void>(Parse(json));
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
