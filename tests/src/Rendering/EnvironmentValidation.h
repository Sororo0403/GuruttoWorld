#pragma once
#include "../Support/TestGameContent.h"
#include "../Support/GpuPixel.h"
#include <SceneRuntime/Scene/SceneEnvironment.h>
#include "../../../Editor/src/Editing/EditState.h"
#include "../../../Editor/src/Editing/EditHistory.h"
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <Engine/Graphics/Resources/RenderTexture.h>
#include <Engine/Graphics/Renderers/MeshRenderer.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <utility>

namespace EnvironmentValidation
{
    inline void Require(bool success, const char* message)
    {
        if (!success)
            throw std::runtime_error(message);
    }
    inline std::array<unsigned char, 4> Pixel(Engine::DirectX12Renderer& renderer,
                                              const std::function<void(ID3D12GraphicsCommandList*)>& draw)
    {
        return TestSupport::Pixel(renderer, draw);
    }
    inline SceneRuntime::SceneLayout Layout()
    {
        SceneRuntime::ScenePlacement camera;
        camera.id = "camera";
        camera.name = "Camera";
        camera.camera.emplace();
        camera.sky.emplace();
        camera.sky->horizon = {0, 0, 1};
        camera.sky->zenith = {0, 0, 1};
        camera.sky->sunStrength = 0;
        camera.sky->cloudOpacity = 0;
        camera.directionalLight.emplace();
        camera.directionalLight->intensity = 0;
        camera.directionalLight->ambient = 1;
        SceneRuntime::SceneLayout layout;
        layout.objects = {camera};
        layout.settings.mainCamera = camera.id;
        return layout;
    }
    inline void Editing(SceneRuntime::SceneWorld& world, const std::filesystem::path& root)
    {
        const auto before = world.Layout().Serialize();
        auto camera = world.Layout().objects[0];
        Editor::EditState state;
        state.Select(camera.id);
        Editor::EditHistory history;
        history.Reset({before, camera.id, state.SelectedIds()});
        std::string error;
        camera.particleEmitter.emplace();
        camera.particleEmitter->count = 7;
        camera.cameraSway.emplace();
        camera.sky->horizon = {0.2f, 0.4f, 0.6f};
        Require(world.SetComponents(camera.id, camera, root, error), "environment property edit accepted");
        history.Observe({world.Layout().Serialize(), camera.id, state.SelectedIds()}, "component/environment");
        camera.sky->horizon = {0.3f, 0.5f, 0.7f};
        camera.camera->verticalFov = 60;
        Require(world.SetComponents(camera.id, camera, root, error), "second environment drag edit accepted");
        history.Observe({world.Layout().Serialize(), camera.id, state.SelectedIds()}, "component/environment");
        history.Commit();
        Require(world.ReplaceLayout(SceneRuntime::SceneLayout::Parse(history.Target(false).json), root, error),
                "environment Undo applies");
        history.Applied(false);
        Require(world.Layout().Serialize() == before && !history.CanUndo(),
                "one Undo restores the full environment drag");
        Require(world.ReplaceLayout(SceneRuntime::SceneLayout::Parse(history.Target(true).json), root, error),
                "environment Redo applies");
        history.Applied(true);
        Require(world.Layout().objects[0].SameComponents(camera),
                "Redo restores Camera Sky Emitter and Sway settings together");
        Editor::ObjectRequest request;
        request.action = Editor::ObjectAction::Settings;
        request.settings = world.Layout().settings;
        request.settings->background = {0.1f, 0.2f, 0.3f, 1};
        request.settings->fog = {true, {0.2f, 0.3f, 0.4f}, 3, 30, 0.5f};
        request.interaction = "settings/environment";
        state.Request(request);
        const auto queued = state.TakeRequest();
        Require(queued && queued->settings && world.SetSettings(*queued->settings, error),
                "typed scene settings command applies");
        history.Observe({world.Layout().Serialize(), camera.id, state.SelectedIds()}, queued->interaction);
        history.Commit();
        const auto edited = world.Layout().Serialize();
        const auto path = root / "environment-edited.json";
        world.Layout().Save(path);
        Require(SceneRuntime::SceneLayout::Load(path).Serialize() == edited,
                "save and reload preserve all authored environment properties");
        std::vector<std::string> copies;
        Require(world.DuplicateObjects({camera.id}, {4, 0, 0}, copies, error) &&
                    world.Layout().objects.back().SameComponents(camera) &&
                    world.Layout().settings.mainCamera == camera.id,
                "duplicate preserves all Component IDs and settings without replacing the main camera reference");
        history.Observe({world.Layout().Serialize(), camera.id, state.SelectedIds()}, {});
        Require(state.DeleteObjects(world, {camera.id}, error) && world.Layout().settings.mainCamera.empty(),
                "deleting an authored camera clears its reference without discarding scene settings");
        history.Observe({world.Layout().Serialize(), state.SelectedId(), state.SelectedIds()}, {});
        Require(world.ReplaceLayout(SceneRuntime::SceneLayout::Parse(history.Target(false).json), root, error) &&
                    world.Layout().settings == queued->settings.value() &&
                    world.Layout().objects[0].SameComponents(camera),
                "Undo restores deleted environment Components and the explicit main camera reference");
    }
    inline void ScaleNormals(Engine::DirectX12Renderer& renderer, const std::filesystem::path& content)
    {
        Engine::Camera camera;
        camera.SetPosition({0, 0, 0});
        camera.SetPerspective(DirectX::XM_PIDIV4, 2, 0.1f, 220);
        Engine::DirectionalLight light;
        light.direction = {0, 0, 1};
        light.ambientIntensity = 0;
        light.intensity = 1;
        light.specularStrength = 0;
        const std::array<std::array<float, 2>, 2> cases{{{1e-11f, 1e12f}, {1e13f, 1e-12f}}};
        for (const auto& values : cases)
        {
            const auto extent = values[0], scale = values[1];
            Engine::MeshData data;
            data.vertices = {{{-extent, -extent, 0}, {0, 0, -1}, {0, 0}},
                             {{0, extent, 0}, {0, 0, -1}, {0, 0}},
                             {{extent, -extent, 0}, {0, 0, -1}, {0, 0}}};
            data.indices = {0, 1, 2};
            Engine::MeshRenderer mesh;
            Require(
                mesh.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), data, content / "Shaders/Mesh.hlsl"),
                "normal scale fixture initializes");
            DirectX::XMFLOAT4X4 world;
            DirectX::XMStoreFloat4x4(&world, DirectX::XMMatrixScaling(scale, scale, scale) *
                                                 DirectX::XMMatrixTranslation(0, 0, 10));
            const auto pixel =
                Pixel(renderer,
                      [&](auto* commands)
                      {
                          mesh.Draw(commands, world, camera.GetViewProjectionMatrix(), light, camera.GetPosition());
                      });
            Require(pixel == std::array<unsigned char, 4>{255, 255, 255, 255},
                    "large and tiny world scales preserve directional lighting without normal overflow or underflow");
        }
    }
    inline void Run(Engine::DirectX12Renderer& renderer)
    {
        const auto content = std::filesystem::absolute(WP1_TEST_GAME_ROOT);
        const auto root = std::filesystem::absolute("generated/tests/environment");
        std::filesystem::create_directories(root / "Assets/Models");
        auto layout = Layout();
        std::string error;
        SceneRuntime::SceneWorld world;
        SceneRuntime::ScenePresentation presentation;
        Require(world.Initialize(renderer, root, layout, content / "Shaders/Mesh.hlsl", &error) &&
                    presentation.Initialize(renderer, content, error),
                "environment color fixture initializes");
        const auto capture = [&]()
        {
            Require(presentation.Prepare(renderer, content, world.Layout(), error),
                    "environment resources prepare before draw");
            return Pixel(renderer,
                         [&](auto* commands)
                         {
                             presentation.Draw(commands, world, 64, 32);
                         });
        };
        Require(capture() == std::array<unsigned char, 4>{0, 0, 255, 255}, "authored blue sky reaches pixel shader");
        auto camera = world.Layout().objects[0];
        camera.sky->horizon = {0, 1, 0};
        camera.sky->zenith = {0, 1, 0};
        Require(world.SetComponents(camera.id, camera, root, error), "sky colors edit without rebuilding resources");
        Require(capture() == std::array<unsigned char, 4>{0, 255, 0, 255}, "edited sky color updates immediately");
        SceneRuntime::SceneEnvironment runtime;
        Require(runtime.Initialize(renderer, content, world.Layout(), error), "runtime copies authored environment");
        Require(Pixel(renderer,
                      [&](auto* commands)
                      {
                          runtime.Draw(commands, 64, 32);
                      }) == capture(),
                "Game runtime and editing preview produce the same authored sky pixels");
        camera.sky->enabled = false;
        Require(world.SetComponents(camera.id, camera, root, error) &&
                    capture() == std::array<unsigned char, 4>{0, 0, 0, 255},
                "disabled Sky leaves the clear background with no hidden title sky");
        Require(Pixel(renderer,
                      [&](auto* commands)
                      {
                          runtime.Draw(commands, 64, 32);
                      }) == std::array<unsigned char, 4>{0, 255, 0, 255},
                "later editor changes do not leak into Play snapshot");
        {
            std::ofstream material(root / "Assets/Models/triangle.mtl");
            material << "newmtl white\nKd 1 1 1\n";
        }
        {
            std::ofstream model(root / "Assets/Models/triangle.obj");
            model << "mtllib triangle.mtl\nusemtl white\nv -10 -10 0\nv 0 10 0\nv 10 -10 0\nf 1 2 3\nf 3 2 1\n";
        }
        SceneRuntime::ScenePlacement mesh;
        mesh.id = "mesh";
        mesh.name = "Mesh";
        mesh.SetModel("Assets/Models/triangle.obj");
        mesh.position = {0, 0, 10};
        std::string created;
        Require(world.AddObject(mesh, root, created, error), "fog test geometry initializes");
        Require(capture() == std::array<unsigned char, 4>{255, 255, 255, 255},
                "ambient light shows unfogged white surface");
        auto settings = world.Layout().settings;
        settings.fog = {true, {1, 0, 0}, 0, 1, 1};
        Require(world.SetSettings(settings, error) && capture() == std::array<unsigned char, 4>{255, 0, 0, 255},
                "authored fog color range and strength reach mesh pixels");
        settings.fog.enabled = false;
        Require(world.SetSettings(settings, error) && capture() == std::array<unsigned char, 4>{255, 255, 255, 255},
                "disabling fog removes its effect without changing material or lighting");
        ScaleNormals(renderer, content);
        Editing(world, root);
    }
}
