#include <SceneRuntime/ScriptModule.h>
#include <Engine/Core/Json.h>
#include <Engine/Assets/AssetDatabase.h>
#include <Windows.h>
#include <fstream>
#include <atomic>
#include <cctype>
#include <type_traits>

namespace
{
    struct ModuleLifetime
    {
        std::filesystem::path path;
        HMODULE handle = nullptr;
        ~ModuleLifetime()
        {
            if (handle)
                FreeLibrary(handle);
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    };
    struct PendingModule
    {
        std::shared_ptr<ModuleLifetime> lifetime;
        std::map<std::string, SceneRuntime::ScriptDefinition> definitions;
        std::string error;
    };
    bool CollectScript(void* context, const char* name, const SceneRuntime::ScriptDefinition* definition)
    {
        auto& pending = *static_cast<PendingModule*>(context);
        try
        {
            if (!name || !definition || pending.definitions.size() >= 256)
                throw std::runtime_error("Invalid script module entry");
            if (!pending.definitions.emplace(name, *definition).second)
                throw std::runtime_error("Duplicate script module behaviour");
            return true;
        }
        catch (const std::exception& error)
        {
            pending.error = error.what();
            return false;
        }
        catch (...)
        {
            pending.error = "Script module registration failed";
            return false;
        }
    }
    template <class Context> struct ModuleCallback
    {
        // このメンバーの所有権を保持し、コールバックのコードがある DLL のアンロードを防ぎます。
        // メンバーは宣言と逆順に破棄されるため、callback を lifetime より後に宣言します。
        // cppcheck-suppress unusedStructMember
        std::shared_ptr<ModuleLifetime> lifetime;
        std::filesystem::path contentRoot;
        std::function<void(Context&)> callback;
        void operator()(Context& original) const
        {
            auto context = original;
            Invoke(context);
        }
        void Invoke(std::remove_const_t<Context>& context) const
        {
            if (context.content)
                context.content->RequireRoot(contentRoot);
            context.contentRoot = contentRoot;
            callback(context);
        }
    };
    void RetainModule(SceneRuntime::ScriptDefinition& definition, const std::shared_ptr<ModuleLifetime>& lifetime,
                      const std::filesystem::path& contentRoot)
    {
        for (auto* callback : {&definition.start, &definition.update, &definition.prepareStop, &definition.onEvent,
                               &definition.fixedUpdate, &definition.lateUpdate})
            if (*callback)
                *callback = ModuleCallback<SceneRuntime::ScriptContext>{lifetime, contentRoot, std::move(*callback)};
        if (definition.stop)
            definition.stop = ModuleCallback<const SceneRuntime::ScriptStopContext>{lifetime, contentRoot,
                                                                                    std::move(definition.stop)};
        if (definition.pause)
        {
            // 関数オブジェクトの破棄が終わるまで、そのコードを持つ DLL を保持します。
            struct PauseCallback
            {
                // callback を先に破棄できるよう、DLL の所有権を保持する lifetime を先に宣言します。
                // cppcheck-suppress unusedStructMember
                std::shared_ptr<ModuleLifetime> lifetime;
                std::filesystem::path contentRoot;
                std::function<void(SceneRuntime::ScriptContext&, bool)> callback;
                void operator()(SceneRuntime::ScriptContext& context, bool paused) const
                {
                    if (context.content)
                        context.content->RequireRoot(contentRoot);
                    context.contentRoot = contentRoot;
                    callback(context, paused);
                }
            };
            definition.pause = PauseCallback{lifetime, contentRoot, std::move(definition.pause)};
        }
    }
    bool SourceName(const std::string& name)
    {
        if (name.empty() || name.size() > 64 || std::isdigit(static_cast<unsigned char>(name.front())))
            return false;
        return std::all_of(name.begin(), name.end(),
                           [](unsigned char c)
                           {
                               return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                                      c == '_';
                           });
    }
    bool ModuleFilename(const std::string& filename)
    {
        return filename.size() <= 128 && filename.starts_with("GameScripts-") && filename.ends_with(".dll") &&
               filename.find_first_of("/\\:") == std::string::npos && filename.find("..") == std::string::npos;
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
    static void ValidateModuleSchemas(const std::map<std::string, ScriptDefinition>& definitions,
                                      const std::map<std::string, ScriptSchema>& schemas)
    {
        for (const auto& [name, definition] : definitions)
        {
            const auto declared = schemas.find(name);
            if (declared == schemas.end())
                throw std::runtime_error("Script schema declaration is missing: " + name);
            if (declared->second != static_cast<const ScriptSchema&>(definition))
                throw std::runtime_error("Script schema differs from the compiled module; rebuild scripts: " + name);
        }
    }
    bool ScriptModule::Reload(const std::filesystem::path& content, std::string& error)
    {
        try
        {
            const auto folder = content / "Assets/Scripts/Bin" / Configuration();
            const auto owner = Engine::AssetDatabase::Text(std::filesystem::weakly_canonical(content));
            const auto manifestPath = folder / "module.json";
            if (!std::filesystem::exists(manifestPath))
                return ScriptRegistry::InstallModule(owner, {}, error);
            std::ifstream stream(manifestPath);
            const auto manifest = Engine::Json::parse(stream);
            if (manifest.at("version").get<unsigned int>() != 1 ||
                manifest.at("configuration").get<std::string>() != Configuration())
                throw std::runtime_error("Script module manifest configuration mismatch");
            const auto filename = manifest.at("file").get<std::string>();
            if (filename.empty())
                return ScriptRegistry::InstallModule(owner, {}, error);
            if (!ModuleFilename(filename))
                throw std::runtime_error("Invalid script module filename");
            PendingModule pending;
            pending.lifetime = std::make_shared<ModuleLifetime>();
            static std::atomic<unsigned int> serial{0};
            const auto copies = std::filesystem::temp_directory_path() / "WP1-script-modules";
            std::filesystem::create_directories(copies);
            pending.lifetime->path =
                copies / (std::to_string(GetCurrentProcessId()) + "-" + std::to_string(++serial) + "-" + filename);
            std::filesystem::copy_file(folder / filename, pending.lifetime->path);
            pending.lifetime->handle =
                LoadLibraryExW(pending.lifetime->path.c_str(), nullptr,
                               LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if (!pending.lifetime->handle)
                throw std::runtime_error("Cannot load script module (" + std::to_string(GetLastError()) + ")");
            const auto entry =
                reinterpret_cast<RegisterScriptModule>(GetProcAddress(pending.lifetime->handle, "Wp1RegisterScripts"));
            if (!entry)
                throw std::runtime_error("Script module export is missing");
            const ScriptModuleAbi abi;
            if (!entry(&abi, &pending, CollectScript))
                throw std::runtime_error(pending.error.empty() ? "Script module ABI or registration mismatch"
                                                               : pending.error);
            const auto schemas = ScriptSchema::LoadProject(content);
            ValidateModuleSchemas(pending.definitions, schemas);
            const auto contentRoot = std::filesystem::weakly_canonical(content);
            for (auto& [name, definition] : pending.definitions)
            {
                static_cast<void>(name);
                RetainModule(definition, pending.lifetime, contentRoot);
            }
            return ScriptRegistry::InstallModule(owner, std::move(pending.definitions), error);
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return false;
        }
        catch (...)
        {
            error = "Script module failed to register";
            return false;
        }
    }
    bool ScriptModule::CreateSource(const std::filesystem::path& content, const std::string& name,
                                    std::filesystem::path& created, std::string& error)
    {
        try
        {
            if (!SourceName(name))
                throw std::runtime_error("Use a C++ identifier of up to 64 ASCII characters");
            const auto path = content / "Assets/Scripts" / (name + ".cpp");
            std::filesystem::create_directories(path.parent_path());
            const auto schemaPath = path.parent_path() / (name + ".schema.json");
            const auto generatedPath = path.parent_path() / (name + ".generated.h");
            if (std::filesystem::exists(path) || std::filesystem::exists(schemaPath) ||
                std::filesystem::exists(generatedPath))
                throw std::runtime_error("Script source or declaration already exists");
            const std::string source =
                "#include <SceneRuntime/ScriptModuleApi.h>\r\n#include \"" + name +
                ".generated.h\"\r\n\r\n"
                "bool Register_" +
                name +
                "(void* context,SceneRuntime::RegisterModuleScript add)\r\n{\r\n"
                "    auto script=SceneRuntime::GeneratedSchemas::" +
                name +
                "Schema();\r\n"
                "    script.update=[](SceneRuntime::ScriptContext& c)\r\n    {\r\n"
                "        if (std::get<bool>(c.Data(\"active\")->value))\r\n"
                "            c.object.position[0]+=c.Value(\"speed\",2)*static_cast<float>(c.seconds);\r\n"
                "    };\r\n"
                "    return add(context,\"" +
                name + "\",&script);\r\n}\r\n";
            const Engine::Json schema{
                {"version", 1},
                {"scripts",
                 {{name,
                   {{"parameters", {{"speed", {{"initial", 2}, {"minimum", 0}, {"maximum", 100}}}}},
                    {"data",
                     {{"active", {{"kind", "bool"}, {"value", true}}},
                      {"label", {{"kind", "string"}, {"value", name}}}}},
                    {"purposes", Engine::Json::object()}}}}}};
            const auto schemaText = schema.dump(2) + "\n";
            Engine::FileContents::Write(schemaPath, schemaText, false);
            try
            {
                Engine::FileContents::Write(path, source, false);
            }
            catch (...)
            {
                if (Engine::FileContents::Read(schemaPath) == schemaText)
                    Engine::FileContents::Remove(schemaPath);
                throw;
            }
            created = path;
            error.clear();
            return true;
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return false;
        }
    }
}
