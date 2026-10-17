#pragma once
#include <SceneRuntime/SceneEnvironment.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <Engine/Platform/Window.h>
#include <Engine/Core/Log.h>
#include <d3d12sdklayers.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace AllScenesValidation
{
    inline void Check(bool condition, const std::string& message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }
    inline void CheckDiagnostics(ID3D12Device* device, const std::string& scene)
    {
        for (const auto& entry : Engine::Log::Recent())
            Check(entry.level != Engine::LogLevel::Error && entry.level != Engine::LogLevel::Warning,
                  scene + ": " + entry.text);
        Microsoft::WRL::ComPtr<ID3D12InfoQueue> queue;
        if (FAILED(device->QueryInterface(IID_PPV_ARGS(&queue))))
            return;
        for (UINT64 i = 0; i < queue->GetNumStoredMessages(); ++i)
        {
            SIZE_T size = 0;
            Check(SUCCEEDED(queue->GetMessage(i, nullptr, &size)), "GPU diagnostic size");
            std::vector<unsigned char> buffer(size);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(buffer.data());
            Check(SUCCEEDED(queue->GetMessage(i, message, &size)), "GPU diagnostic read");
            Check(message->Severity != D3D12_MESSAGE_SEVERITY_ERROR &&
                      message->Severity != D3D12_MESSAGE_SEVERITY_CORRUPTION,
                  scene + ": " + message->pDescription);
        }
        queue->ClearStoredMessages();
    }
    inline void Run()
    {
        const auto root = std::filesystem::absolute("Games/GuruttoWorld");
        const auto output = std::filesystem::absolute("generated/tests/all-scenes");
        std::filesystem::create_directories(output);
        std::vector<std::filesystem::path> paths;
        for (const auto& entry : std::filesystem::directory_iterator(root / "Assets/Scenes"))
            if (entry.path().extension() == ".json")
                paths.push_back(entry.path());
        std::sort(paths.begin(), paths.end());
        Engine::Window window;
        Engine::DirectX12Renderer renderer;
        Check(window.Create(L"All authored scenes validation", 640, 360) && renderer.Initialize(window.GetHandle()),
              "All scenes renderer initializes");
        for (const auto& path : paths)
        {
            Engine::Log::ClearRecent();
            const auto label = path.filename().string();
            auto layout = SceneRuntime::SceneLayout::Load(path, root);
            const auto original = layout.Serialize();
            layout.Save(output / path.filename());
            auto restored = SceneRuntime::SceneLayout::Load(output / path.filename(), root);
            Check(restored.Serialize() == original, label + ": saved scene changed authored data");
            SceneRuntime::SceneEnvironment scene;
            std::string error;
            if (!scene.Initialize(renderer, root, std::move(restored), error) || !scene.StartAudio(root, error))
                throw std::runtime_error(label + ": " + error);
            for (int frame = 0; frame < 6; ++frame)
            {
                Check(scene.MovePlayers(scene.World().PhysicsStepSeconds(), 0, 0, false),
                      label + ": physics update failed");
                scene.Update(scene.World().PhysicsStepSeconds(), true, true);
                scene.UpdateAudio(true);
                Check(renderer.Render(layout.settings.background,
                                      [&](auto* commands, float)
                                      {
                                          scene.Draw(commands, 640, 360);
                                      }) != Engine::RenderResult::Failed,
                      label + ": rendering failed");
            }
            Check(renderer.WaitForIdle(), label + ": GPU completion failed");
            CheckDiagnostics(renderer.GetDevice(), label);
            Check(SceneRuntime::SceneLayout::Load(path, root).Serialize() == original,
                  label + ": playback changed authored scene");
            std::cout << "PASS scene: " << label << std::endl;
        }
        std::cout << "PASS: " << paths.size()
                  << " existing scenes save/reload, physics, scripts, animation, audio and D3D12 rendering\n";
    }
}
