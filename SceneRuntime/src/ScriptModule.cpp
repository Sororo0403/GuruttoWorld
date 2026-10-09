#include <SceneRuntime/ScriptModule.h>
#include <Engine/Core/Json.h>
#include <Engine/Assets/AssetDatabase.h>
#include <Windows.h>
#include <fstream>
#include <atomic>
#include <cctype>

namespace
{
    struct ModuleLifetime
    {
        std::filesystem::path path;
        HMODULE handle=nullptr;
        ~ModuleLifetime()
        {
            if (handle) FreeLibrary(handle);
            std::error_code ignored; std::filesystem::remove(path,ignored);
        }
    };
    struct PendingModule
    {
        std::shared_ptr<ModuleLifetime> lifetime;
        std::map<std::string,SceneRuntime::ScriptDefinition> definitions;
        std::string error;
    };
    bool CollectScript(void* context,const char* name,const SceneRuntime::ScriptDefinition* definition)
    {
        auto& pending=*static_cast<PendingModule*>(context);
        try {
            if (!name || !definition || pending.definitions.size()>=256) throw std::runtime_error("Invalid script module entry");
            if (!pending.definitions.emplace(name,*definition).second) throw std::runtime_error("Duplicate script module behaviour");
            return true;
        } catch (const std::exception& error) { pending.error=error.what(); return false; }
        catch (...) { pending.error="Script module registration failed"; return false; }
    }
    struct ModuleCallback
    {
        // The callback must be destroyed before releasing its DLL.
        std::shared_ptr<ModuleLifetime> lifetime;
        std::function<void(SceneRuntime::ScriptContext&)> callback;
        void operator()(SceneRuntime::ScriptContext& context) const { callback(context); }
    };
    void RetainModule(SceneRuntime::ScriptDefinition& definition,const std::shared_ptr<ModuleLifetime>& lifetime)
    {
        for (auto* callback:{&definition.start,&definition.update,&definition.stop,&definition.onEvent,&definition.fixedUpdate,&definition.lateUpdate})
            if (*callback) *callback=ModuleCallback{lifetime,std::move(*callback)};
    }
    bool SourceName(const std::string& name)
    {
        if (name.empty() || name.size()>64 || std::isdigit(static_cast<unsigned char>(name.front()))) return false;
        return std::all_of(name.begin(),name.end(),[](unsigned char c) { return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_'; });
    }
    bool ModuleFilename(const std::string& filename)
    {
        return filename.size()<=128 && filename.starts_with("GameScripts-") && filename.ends_with(".dll") &&
            filename.find_first_of("/\\:")==std::string::npos && filename.find("..")==std::string::npos;
    }
}
namespace SceneRuntime
{
    const char* ScriptModule::Configuration()
    {
#if defined(_DEBUG)
        return "Debug";
#elif defined(ENGINE_DEVELOPMENT)
        return "Development";
#else
        return "Release";
#endif
    }
    bool ScriptModule::Reload(const std::filesystem::path& content,std::string& error)
    {
        try {
            const auto folder=content/"Assets/Scripts/Bin"/Configuration();
            const auto owner=Engine::AssetDatabase::Text(std::filesystem::weakly_canonical(content));
            const auto manifestPath=folder/"module.json";
            if (!std::filesystem::exists(manifestPath)) return ScriptRegistry::InstallModule(owner,{},error);
            std::ifstream stream(manifestPath); const auto manifest=Engine::Json::parse(stream);
            if (manifest.at("version").get<unsigned int>()!=1 || manifest.at("configuration").get<std::string>()!=Configuration())
                throw std::runtime_error("Script module manifest configuration mismatch");
            const auto filename=manifest.at("file").get<std::string>();
            if (filename.empty()) return ScriptRegistry::InstallModule(owner,{},error);
            if (!ModuleFilename(filename))
                throw std::runtime_error("Invalid script module filename");
            PendingModule pending; pending.lifetime=std::make_shared<ModuleLifetime>();
            static std::atomic<unsigned int> serial{0};
            const auto copies=std::filesystem::temp_directory_path()/"WP1-script-modules";
            std::filesystem::create_directories(copies);
            pending.lifetime->path=copies/(std::to_string(GetCurrentProcessId())+"-"+std::to_string(++serial)+"-"+filename);
            std::filesystem::copy_file(folder/filename,pending.lifetime->path);
            pending.lifetime->handle=LoadLibraryExW(pending.lifetime->path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if (!pending.lifetime->handle) throw std::runtime_error("Cannot load script module ("+std::to_string(GetLastError())+")");
            const auto entry=reinterpret_cast<RegisterScriptModule>(GetProcAddress(pending.lifetime->handle,"Wp1RegisterScripts"));
            if (!entry) throw std::runtime_error("Script module export is missing");
            const ScriptModuleAbi abi;
            if (!entry(&abi,&pending,CollectScript)) throw std::runtime_error(pending.error.empty() ? "Script module ABI or registration mismatch" : pending.error);
            for (auto& [name,definition]:pending.definitions) { static_cast<void>(name); RetainModule(definition,pending.lifetime); }
            return ScriptRegistry::InstallModule(owner,std::move(pending.definitions),error);
        } catch (const std::exception& exception) { error=exception.what(); return false; }
        catch (...) { error="Script module failed to register"; return false; }
    }
    bool ScriptModule::CreateSource(const std::filesystem::path& content,const std::string& name,std::filesystem::path& created,std::string& error)
    {
        try {
            if (!SourceName(name)) throw std::runtime_error("Use a C++ identifier of up to 64 ASCII characters");
            const auto path=content/"Assets/Scripts"/(name+".cpp");
            std::filesystem::create_directories(path.parent_path());
            const std::string source="#include <SceneRuntime/ScriptModuleApi.h>\r\n\r\n"
                "bool Register_"+name+"(void* context,SceneRuntime::RegisterModuleScript add)\r\n{\r\n"
                "    SceneRuntime::ScriptDefinition script;\r\n"
                "    script.fields={{\"speed\",{2,0,100}}};\r\n"
                "    script.dataFields={{\"active\",{true}},{\"label\",{std::string(\""+name+"\")}}};\r\n"
                "    script.update=[](SceneRuntime::ScriptContext& c) {\r\n"
                "        if (std::get<bool>(c.Data(\"active\")->value))\r\n"
                "            c.object.position[0]+=c.Value(\"speed\",2)*static_cast<float>(c.seconds);\r\n"
                "    };\r\n"
                "    return add(context,\""+name+"\",&script);\r\n}\r\n";
            HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            if (file==INVALID_HANDLE_VALUE) throw std::runtime_error("Script file already exists or cannot be created");
            DWORD written=0; const bool saved=WriteFile(file,source.data(),static_cast<DWORD>(source.size()),&written,nullptr) && written==source.size() && FlushFileBuffers(file);
            CloseHandle(file);
            if (!saved) { DeleteFileW(path.c_str()); throw std::runtime_error("Cannot save script source"); }
            created=path; error.clear(); return true;
        } catch (const std::exception& exception) { error=exception.what(); return false; }
    }
}
