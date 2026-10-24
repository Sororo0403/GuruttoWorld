#pragma once
#include <SceneRuntime/Scripts/ScriptModule.h>
#include <SceneRuntime/Scripts/ScriptRuntime.h>
#include <Engine/Core/FileContents.h>
#include <Engine/Core/Json.h>

namespace ScriptModulePathValidation
{
    /// <summary>DLL名の不正文字・NUL・空ID・過長IDを、ファイル操作や登録変更より前に拒否します。</summary>
    inline void Run()
    {
        const auto root = std::filesystem::absolute("generated/tests/module-paths") /
                          (std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
        const auto folder = root / "Assets/Scripts/Bin" / SceneRuntime::ScriptModule::Configuration();
        std::filesystem::create_directories(folder);
        const auto revision = SceneRuntime::ScriptRegistry::Revision();
        const std::vector<std::string> invalid{std::string("GameScripts-valid.dll") + '\0' + "other.dll",
                                               "GameScripts-bad?.dll",
                                               "GameScripts-.dll",
                                               "GameScripts-new\nline.dll",
                                               "GameScripts-../file.dll",
                                               "GameScripts-" + std::string(65, 'x') + ".dll"};
        for (const auto& filename : invalid)
        {
            Engine::FileContents::Write(folder / "module.json",
                                        Engine::Json{{"version", 1},
                                                     {"configuration", SceneRuntime::ScriptModule::Configuration()},
                                                     {"file", filename}}
                                            .dump());
            std::string error;
            if (SceneRuntime::ScriptModule::Reload(root, error) || error != "Invalid script module filename" ||
                SceneRuntime::ScriptRegistry::Revision() != revision)
                throw std::runtime_error("unsafe module filename reached file loading or changed the registry");
        }
        Engine::FileContents::Write(folder / "module.json",
                                    Engine::Json{{"version", 1},
                                                 {"configuration", SceneRuntime::ScriptModule::Configuration()},
                                                 {"file", "GameScripts-safe_name-012.dll"}}
                                        .dump());
        std::string error;
        if (SceneRuntime::ScriptModule::Reload(root, error) || error == "Invalid script module filename" ||
            SceneRuntime::ScriptRegistry::Revision() != revision)
            throw std::runtime_error("canonical module filename was rejected or registry changed on load failure");
    }
}
