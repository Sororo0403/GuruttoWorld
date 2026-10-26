#include "ChangeCaptureValidation.h"
#include <SceneRuntime/Scripts/ScriptModule.h>
#include <Engine/Core/FileContents.h>
#include <Engine/Core/Json.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <Engine/Platform/Window.h>
#include <iostream>

int main(int argc, char** argv)
{
    try
    {
        const auto root = std::filesystem::absolute("generated/tests/change-capture") /
                          (std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
        std::filesystem::create_directories(root / "Engine");
        std::filesystem::copy("Engine/Shaders", root / "Engine/Shaders", std::filesystem::copy_options::recursive);
        unsigned int count = 0;
        const auto contract = [&](const std::string& id, bool ok)
        {
            if (!ok)
                throw std::runtime_error("FAIL: " + id);
            ++count;
            std::cout << "PASS: " << id << '\n';
        };
        ChangeCaptureValidation::Run(root, contract);
        if (argc == 2)
        {
            std::string error;
            contract("capture-dll-load", SceneRuntime::ScriptModule::Reload(argv[1], error));
            SceneRuntime::SceneLayout layout;
            SceneRuntime::ScenePlacement owner;
            owner.id = "dll-owner";
            owner.scripts.push_back({"capture", true, "CaptureTransformDll", {}});
            layout.objects = {owner};
            SceneRuntime::ScriptRuntime runtime;
            contract("capture-dll-execute",
                     runtime.Update(layout, .016, error) && layout.objects.front().position[0] == 2 &&
                         layout.objects.front().pointLight && layout.objects.front().boxCollider);
            runtime.Stop(layout);
            const auto moduleRoot =
                std::filesystem::absolute(argv[1]) / "Assets/Scripts/Bin" / SceneRuntime::ScriptModule::Configuration();
            const auto manifest = Engine::FileContents::Read(moduleRoot / "module.json");
            contract("capture-dll-manifest", manifest.has_value());
            const auto modulePath = moduleRoot / Engine::Json::parse(*manifest).at("file").get<std::string>();
            struct NativeModule
            {
                HMODULE handle = nullptr;
                ~NativeModule()
                {
                    if (handle)
                        FreeLibrary(handle);
                }
            } module{LoadLibraryExW(modulePath.c_str(), nullptr,
                                    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)};
            const auto entry = module.handle ? reinterpret_cast<SceneRuntime::RegisterScriptModule>(
                                                   GetProcAddress(module.handle, "Wp1RegisterScripts"))
                                             : nullptr;
            auto previousAbi = SceneRuntime::ScriptModuleAbi{};
            previousAbi.version = 15;
            unsigned int registrations = 0;
            contract("capture-dll-rejects-old-abi",
                     entry &&
                         !entry(&previousAbi, &registrations,
                                [](void* count, const char*, const SceneRuntime::ScriptCallbacks*)
                                {
                                    ++*static_cast<unsigned int*>(count);
                                    return true;
                                }) &&
                         registrations == 0);
        }
        Engine::Window window;
        Engine::DirectX12Renderer renderer;
        contract("capture-renderer",
                 window.Create(L"Transform change capture", 64, 64) && renderer.Initialize(window.GetHandle()));
        ChangeCaptureValidation::Runtime(renderer, root, contract);
        contract("capture-gpu-completion", renderer.WaitForIdle());
        std::cout << "Change capture contracts: " << count << ", failures: 0\n";
        return 0;
    }
    catch (const std::exception& failure)
    {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
