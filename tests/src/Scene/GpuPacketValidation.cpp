#include <SceneRuntime/Scene/SceneWorld.h>
#include <SceneRuntime/Rendering/RenderScene.h>
#include "../../../SceneRuntime/src/Rendering/SceneRenderResources.h"
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <Engine/Graphics/Resources/RenderFrameContext.h>
#include <Engine/Platform/Window.h>
#include <iostream>
#include <Engine/Core/Profiler.h>

namespace
{
    size_t contracts = 0;
    void Require(bool value, const char* name)
    {
        ++contracts;
        if (!value)
            throw std::runtime_error(name);
        std::cout << "PASS: " << name << std::endl;
    }
    struct Resource
    {
        int& destroyed;
        explicit Resource(int& count) : destroyed(count) {}
        ~Resource()
        {
            ++destroyed;
        }
    };
    void ValidateFrames()
    {
        int destroyed = 0;
        Engine::Window window;
        Engine::DirectX12Renderer renderer;
        Require(window.Create(L"Packet lifetime validation", 64, 64) && renderer.Initialize(window.GetHandle()),
                "gpu-packet-renderer");
        window.Show();
        auto resource = std::make_shared<Resource>(destroyed);
        bool retained = false;
        const auto rendered = renderer.Render({0, 0, 0, 1},
                                              [&](auto* commands, float)
                                              {
                                                  retained = Engine::RenderFrameContext::Retain(commands, resource);
                                                  resource.reset();
                                                  Require(retained && destroyed == 0,
                                                          "gpu-packet-keeps-recording-resource-after-owner-release");
                                              });
        Require(rendered != Engine::RenderResult::Failed && retained && destroyed == 0,
                "gpu-packet-keeps-submitted-resource");
        Require(renderer.WaitForIdle(), "gpu-packet-submission-completes");
        for (int frame = 0; frame < 5 && destroyed == 0; ++frame)
            Require(renderer.Render({0, 0, 0, 1}) != Engine::RenderResult::Failed,
                    "gpu-packet-reuses-completed-frame-slot");
        Require(destroyed == 1, "gpu-packet-releases-after-slot-fence-wait");
        auto aborted = std::make_shared<Resource>(destroyed);
        const auto failed = renderer.Render({0, 0, 0, 1},
                                            [&](auto* commands, float)
                                            {
                                                Require(Engine::RenderFrameContext::Retain(commands, aborted),
                                                        "gpu-packet-retains-aborted-frame-candidate");
                                                aborted.reset();
                                                throw std::runtime_error("injected unsubmitted draw failure");
                                            });
        Require(failed == Engine::RenderResult::Failed && destroyed == 2,
                "gpu-packet-aborted-frame-releases-without-submission");
        Require(renderer.Render({0, 0, 0, 1}) != Engine::RenderResult::Failed && renderer.WaitForIdle(),
                "gpu-packet-retries-after-aborted-ui-and-draw-frame");
        Require(!Engine::RenderFrameContext::Retain(nullptr, std::make_shared<int>(1)),
                "gpu-packet-rejects-missing-recording-context");
    }
    void ValidateWorld()
    {
        using namespace SceneRuntime;
        const auto root = std::filesystem::absolute("generated/tests/gpu-packet") /
                          (std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
        std::filesystem::create_directories(root / "Engine");
        std::filesystem::copy("Engine/Shaders", root / "Engine/Shaders", std::filesystem::copy_options::recursive);
        Engine::Window window;
        Engine::DirectX12Renderer renderer;
        Require(window.Create(L"World packet validation", 64, 64) && renderer.Initialize(window.GetHandle()),
                "gpu-packet-world-renderer");
        window.Show();
        SceneLayout layout;
        ScenePlacement ground;
        ground.id = "ground";
        ground.terrain = TerrainComponent{};
        ground.terrain->rows = ground.terrain->columns = 2;
        ground.terrain->heights = {0, 0, 0, 0};
        ground.material.emplace();
        ground.material->surfaceOverride = true;
        ground.animation.emplace();
        AnimationTrack surface;
        surface.property = "material.roughness";
        surface.clock = "surface";
        surface.easing = "linear";
        surface.keys = {{0, {.1f, 0, 0, 0}}, {1, {.9f, 0, 0, 0}}};
        ground.animation->tracks.push_back(surface);
        layout.objects.push_back(ground);
        auto world = std::make_unique<SceneWorld>();
        std::string error;
        Require(world->Initialize(renderer, std::filesystem::relative(root), layout, root / "Engine/Shaders/Mesh.hlsl",
                                  &error) &&
                    world->StartScripts(error),
                "gpu-packet-world-starts-with-display-projection");
        auto first = world->RenderSnapshot();
        Require(first && first->objects.size() == 1 && first->objects.front().matrix._41 == 0,
                "gpu-packet-world-publishes-renderable-generation");
        Engine::CpuProfiler::Take();
        Require(world->UpdateComponents(.016), "gpu-packet-steady-runtime-update");
        auto steady = world->RenderSnapshot();
        const auto scopes = Engine::CpuProfiler::Take();
        Require(std::none_of(scopes.begin(), scopes.end(),
                             [](const auto& scope)
                             {
                                 return scope.name == "Runtime authoring export";
                             }),
                "gpu-packet-steady-runtime-does-not-clone-scene-layout");
        Require(steady->objects.front().binding.resource == first->objects.front().binding.resource &&
                    steady->objects.front().binding.generation == first->objects.front().binding.generation,
                "gpu-packet-steady-runtime-reuses-resource-generation");
        Engine::CpuProfiler::Take();
        Require(world->Animate({{"surface", .25f}}), "gpu-packet-property-animation-commits-surface-value");
        auto surfaceFrame = world->RenderSnapshot();
        const auto* surfaceResource =
            dynamic_cast<const GpuRenderObject*>(surfaceFrame->objects.front().binding.resource.get());
        const auto* originalResource =
            dynamic_cast<const GpuRenderObject*>(first->objects.front().binding.resource.get());
        Require(surfaceResource && originalResource &&
                    std::abs(surfaceResource->object.GetMaterial()->roughness - .3f) < 1e-6f &&
                    originalResource->object.GetMaterial()->roughness == .5f,
                "gpu-packet-property-animation-keeps-immutable-material-generations");
        const auto surfaceScopes = Engine::CpuProfiler::Take();
        Require(std::none_of(surfaceScopes.begin(), surfaceScopes.end(),
                             [](const auto& scope)
                             {
                                 return scope.name == "Runtime authoring export";
                             }),
                "gpu-packet-property-animation-does-not-clone-scene-layout");
        Require(world->Animate({{"surface", .25f}}) && world->RenderSnapshot() == surfaceFrame,
                "gpu-packet-same-property-values-keep-packet-generation");
        Require(world->SetLocalTransform("ground", {1, 0, 0}, {}, {1, 1, 1}), "gpu-packet-world-transform-commits");
        auto second = world->RenderSnapshot();
        Require(second && second != first && first->objects.front().matrix._41 == 0 &&
                    second->objects.front().matrix._41 == 1,
                "gpu-packet-old-view-retains-prior-transform");
        std::weak_ptr<const RenderResource> oldResource = first->objects.front().binding.resource;
        Engine::Camera camera;
        Engine::DirectionalLight light;
        Engine::OcclusionRenderer occlusion;
        MeshTelemetry telemetry;
        bool recorded = false;
        const auto rendered = renderer.Render(
            {0, 0, 0, 1},
            [&](auto* commands, float)
            {
                recorded = RenderScene::Draw(first, commands, camera, light, {}, occlusion, telemetry);
                first.reset();
                steady.reset();
                surfaceFrame.reset();
                second.reset();
                world.reset();
                Require(recorded && !oldResource.expired(), "gpu-packet-world-destruction-keeps-recorded-resource");
            });
        Require(rendered != Engine::RenderResult::Failed && recorded && telemetry.draws == 1 &&
                    telemetry.triangles == 2,
                "gpu-packet-draws-detached-world-generation");
        Require(renderer.WaitForIdle() && !oldResource.expired(),
                "gpu-packet-retains-detached-resource-until-frame-collection");
        for (int frame = 0; frame < 5 && !oldResource.expired(); ++frame)
            Require(renderer.Render({0, 0, 0, 1}) != Engine::RenderResult::Failed,
                    "gpu-packet-collects-detached-frame");
        Require(oldResource.expired(), "gpu-packet-detached-resource-releases-after-fence");
        Require(renderer.WaitForIdle(), "gpu-packet-world-gpu-completion");
        auto document = std::make_shared<SceneDocumentData>(layout);
        auto projection = std::make_unique<SceneWorld>(document);
        Require(projection->Initialize(renderer, root, layout, root / "Engine/Shaders/Mesh.hlsl", &error),
                "gpu-packet-document-projection-initializes");
        auto documentBefore = projection->RenderSnapshot();
        Require(documentBefore && documentBefore->objects.size() == 1, "gpu-packet-document-publishes-snapshot");
        Require(projection->Edit(TransformEdit{"ground", {{2, 0, 0}, {}, {1, 1, 1}}}, error).succeeded,
                "gpu-packet-document-edit-uses-projection-transaction");
        auto documentAfter = projection->RenderSnapshot();
        Require(documentAfter && documentBefore->objects.front().matrix._41 == 0 &&
                    documentAfter->objects.front().matrix._41 == 2,
                "gpu-packet-document-edit-retains-prior-view");
        auto extra = ground;
        extra.id = "extra";
        std::string created;
        Require(projection->AddObject(extra, root, created, error) && projection->RenderSnapshot()->objects.size() == 2,
                "gpu-packet-document-add-updates-snapshot");
        Require(projection->RemoveObjects({"extra"}, error) && projection->RenderSnapshot()->objects.size() == 1,
                "gpu-packet-document-remove-updates-snapshot");
        projection.reset();
        Require(renderer.Render({0, 0, 0, 1},
                                [&](auto* commands, float)
                                {
                                    Require(RenderScene::Draw(documentBefore, commands, camera, light, {}, occlusion,
                                                              telemetry),
                                            "gpu-packet-document-detached-view-draws");
                                }) != Engine::RenderResult::Failed &&
                    renderer.WaitForIdle(),
                "gpu-packet-document-detached-view-completes");
    }
}
int main()
{
    try
    {
        ValidateFrames();
        ValidateWorld();
        std::cout << "GPU packet contracts: " << contracts << ", failures: 0\n";
        return 0;
    }
    catch (const std::exception& failure)
    {
        std::cerr << "FAIL: " << failure.what() << '\n';
        return 1;
    }
}
