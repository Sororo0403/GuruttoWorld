#pragma once
#include <SceneRuntime/SceneEnvironment.h>
#include <SceneRuntime/ProjectSettings.h>
#include <Engine/Core/Log.h>
#include <Engine/Platform/Window.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
namespace App {
inline int ValidatePackage() {
    try {
        std::array<wchar_t,32768> executable{};
        const auto length=GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
        if (!length || length>=executable.size()) return 1;
        const auto root=std::filesystem::path(executable.data()).parent_path();
        Engine::Log::Initialize(root/"Diagnostics/package-validation.log");
        const auto project=SceneRuntime::ProjectSettings::Load(root);
        Engine::Window window; Engine::DirectX12Renderer renderer;
        if (!window.Create(L"WP1 package validation",320,180) || !renderer.Initialize(window.GetHandle())) return 1;
        SceneRuntime::SceneEnvironment scene; std::string error;
        if (!scene.Initialize(renderer,root,root/Engine::AssetDatabase::Path(project.startupScene),error)) { Engine::Log::Error(error); return 1; }
        for (int frame=0;frame<3;++frame) if (renderer.Render(scene.World().Layout().settings.background,[&](auto* commands,float) {
            scene.Draw(commands,320,180);
        })==Engine::RenderResult::Failed) return 1;
        if (!renderer.WaitForIdle()) return 1;
        Engine::Log::Info("Package validation passed: startup scene, models, shaders and UI resources."); return 0;
    } catch (const std::exception& error) { Engine::Log::Error(error.what()); return 1; }
}
}
