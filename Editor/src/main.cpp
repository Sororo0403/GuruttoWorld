#include "CameraPanel.h"
#include "ObjectPanel.h"
#include <SceneRuntime/SceneWorld.h>
#include <Engine/Core/Application.h>
#include <Engine/Core/Log.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <imgui.h>
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace
{
    std::filesystem::path ContentRoot()
    {
        std::wstring executable(32768, L'\0');
        const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (!length || length >= executable.size()) return {};
        executable.resize(length);
        auto directory = std::filesystem::path(executable).parent_path();
        // 開発時は元のContentを使い、将来の保存先もビルド出力のコピーにしません。
        for (auto parent = directory; !parent.empty(); parent = parent.parent_path())
        {
            if (std::filesystem::exists(parent / "Content/Assets/Scenes/TitleStreet.json")) return parent / "Content";
            if (parent == parent.parent_path()) break;
        }
        return directory; // 配布時はEditorに同梱されたContentを読みます。
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    const auto root = ContentRoot();
    if (root.empty()) return 1;
    SceneRuntime::SceneWorld world;
    Engine::DebugCamera camera;
    camera.SetResetPose({ -0.8f, 2.8f, -7.0f }, 0.03f, 0.09f);
    camera.SetMoveSpeed(8.0f);
    Editor::CameraPanel cameraPanel;
    Editor::ObjectPanel objectPanel;
    Engine::DirectionalLight light;
    light.direction = { -0.5f, -0.8f, 0.6f };
    light.color = { 1.0f, 0.95f, 0.84f };
    light.ambientIntensity = 0.52f;
    light.intensity = 0.76f;
    light.specularStrength = 0.03f;
    const Engine::Keyboard* keyboard = nullptr;
    double seconds = 0.0;
    bool initialized = false;
    Engine::ApplicationCallbacks callbacks;
    callbacks.update = [&](double dt, const Engine::Keyboard& input)
    {
        keyboard = &input;
        seconds = dt;
        if (!input.IsActive()) cameraPanel.CancelDrag();
    };
    callbacks.draw = [&](Engine::DirectX12Renderer& renderer)
    {
        if (!initialized)
        {
            if (!world.Initialize(renderer, root, root / "Assets/Scenes/TitleStreet.json",
                root / "Shaders/TitleMesh.hlsl")) return Engine::RenderResult::Failed;
            initialized = true;
        }
        return renderer.Render({ 0.66f, 0.79f, 0.83f, 1.0f }, [&](ID3D12GraphicsCommandList* commands, float aspect)
        {
            const float fov = 2.0f * std::atan(std::tan(DirectX::XM_PIDIV4 * 0.5f) *
                (std::max)(1.0f, (16.0f / 9.0f) / aspect));
            camera.GetCamera().SetPerspective(fov, aspect, 0.1f, 220.0f);
            world.Draw(commands, camera.GetCamera(), light);
        }, [&]()
        {
            ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(340, 110), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Street Editor"))
            {
                ImGui::Text("Objects: %zu", world.Layout().objects.size());
                ImGui::TextUnformatted("Select objects and edit transforms in Inspector.");
                ImGui::TextWrapped("Content: %s", root.string().c_str());
            }
            ImGui::End();
            objectPanel.Draw(world);
            if (keyboard) cameraPanel.Draw(camera, *keyboard, seconds);
        });
    };
    Engine::ApplicationSettings settings;
    settings.title = L"WP1 Street Editor";
    Engine::Application application;
    return application.Run(settings, callbacks);
}
