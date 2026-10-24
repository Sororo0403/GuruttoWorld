#include <SceneRuntime/Scripts/ScriptModuleApi.h>
#include <SceneRuntime/Scripts/ScriptValueJson.h>
#include <Engine/Core/FileContents.h>
#include <iostream>

namespace
{
    bool Collect(void* target, const char* name, const SceneRuntime::ScriptDefinition* definition)
    {
        auto& scripts = *static_cast<Engine::Json*>(target);
        if (!name || !definition || scripts.contains(name))
            return false;
        scripts[name] = definition->Serialize();
        return true;
    }
}

int wmain(int count, wchar_t** arguments)
{
    try
    {
        if (count != 4 && count != 5)
            throw std::runtime_error("Usage: --export|--compare module.dll snapshot.json [project]");
        const bool compare = std::wstring_view(arguments[1]) == L"--compare";
        const bool validate = std::wstring_view(arguments[1]) == L"--validate";
        if (!compare && !validate && std::wstring_view(arguments[1]) != L"--export")
            throw std::runtime_error("Invalid mode");
        struct Module
        {
            HMODULE handle;
            ~Module()
            {
                if (handle)
                    FreeLibrary(handle);
            }
        } module{LoadLibraryExW(std::filesystem::absolute(arguments[2]).c_str(), nullptr,
                                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)};
        if (!module.handle)
            throw std::runtime_error("Cannot load game module");
        const auto entry =
            reinterpret_cast<SceneRuntime::RegisterScriptModule>(GetProcAddress(module.handle, "Wp1RegisterScripts"));
        if (!entry)
            throw std::runtime_error("Missing script registration");
        const SceneRuntime::ScriptModuleAbi abi;
        auto scripts = Engine::Json::object();
        if (!entry(&abi, &scripts, Collect))
            throw std::runtime_error("Incompatible game module or duplicate registration");
        const Engine::Json snapshot{{"version", 1}, {"scripts", scripts}};
        const auto path = std::filesystem::path(arguments[3]);
        if (!compare && !validate)
            Engine::FileContents::Write(path, snapshot.dump(2) + "\n", false);
        else if (compare)
        {
            const auto before = Engine::FileContents::Read(path);
            if (!before || Engine::Json::parse(*before) != snapshot)
                throw std::runtime_error("Game script metadata changed during migration");
        }
        if (validate || count == 5)
        {
            const auto declared = SceneRuntime::ScriptSchema::LoadProject(arguments[validate ? 3 : 4]);
            for (const auto& [name, schema] : scripts.items())
                if (!declared.contains(name) || declared.at(name).Serialize() != schema)
                    throw std::runtime_error("Declared schema differs from native metadata: " + name);
        }
        std::cout << "PASS: " << scripts.size() << " native script schemas preserve metadata"
                  << (validate || count == 5 ? " and match declarations" : "") << '\n';
        return 0;
    }
    catch (const std::exception& failure)
    {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
