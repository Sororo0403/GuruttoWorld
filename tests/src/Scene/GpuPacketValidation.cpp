#include <SceneRuntime/Rendering/EditorProjection.h>
#include <SceneRuntime/Rendering/SceneGpuResources.h>
#include <SceneRuntime/Scene/SceneWorld.h>
#include <SceneRuntime/Rendering/RenderScene.h>
#include <SceneRuntime/Rendering/ScenePresentation.h>
#include <LegacyScripts/ScriptRuntime.h>
#include "../Support/GpuPixel.h"
#include "../../../SceneRuntime/src/Rendering/SceneRenderResources.h"
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <Engine/Graphics/Resources/RenderFrameContext.h>
#include <Engine/Platform/Window.h>
#include <d3d12sdklayers.h>
#include <iostream>
#include <Engine/Core/Profiler.h>
#include <Engine/Animation/SkeletonAssets.h>
#include <Engine/Core/FileContents.h>

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
        auto occlusion = std::make_unique<Engine::OcclusionRenderer>();
        Require(occlusion->Initialize(renderer.GetDevice(), "Engine/Shaders/Mesh.hlsl"),
                "gpu-packet-occlusion-prepares-query-pipeline");
        Microsoft::WRL::ComPtr<ID3D12InfoQueue> diagnostics;
        renderer.GetDevice()->QueryInterface(IID_PPV_ARGS(&diagnostics));
        if (diagnostics)
            diagnostics->ClearStoredMessages();
        Engine::Camera queryCamera;
        Require(queryCamera.SetPerspective(DirectX::XM_PIDIV4, 1, .1f, 100), "gpu-packet-occlusion-camera");
        DirectX::BoundingBox bounds{{0, 0, 3}, {.5f, .5f, .5f}};
        DirectX::XMFLOAT4X4 identity;
        DirectX::XMStoreFloat4x4(&identity, DirectX::XMMatrixIdentity());
        bool began = false;
        const auto abortedQuery = renderer.Render({0, 0, 0, 1},
                                                  [&](auto* commands, float)
                                                  {
                                                      began = occlusion->Begin(commands, bounds, identity,
                                                                               queryCamera.GetViewProjectionMatrix());
                                                      throw std::runtime_error("injected failure before query End");
                                                  });
        Require(abortedQuery == Engine::RenderResult::Failed && began, "gpu-packet-occlusion-aborts-before-end");
        for (int attempt = 0; attempt < 3; ++attempt)
            Require(renderer.Render({0, 0, 0, 1},
                                    [&](auto* commands, float)
                                    {
                                        Require(occlusion->Begin(commands, bounds, identity,
                                                                 queryCamera.GetViewProjectionMatrix()),
                                                "gpu-packet-occlusion-retries-with-balanced-resource-state");
                                        Engine::OcclusionRenderer::End(commands);
                                    }) != Engine::RenderResult::Failed &&
                        renderer.WaitForIdle(),
                    "gpu-packet-occlusion-retry-completes");
        Require(renderer.Render({0, 0, 0, 1},
                                [&](auto* commands, float)
                                {
                                    Require(occlusion->Begin(commands, bounds, identity,
                                                             queryCamera.GetViewProjectionMatrix()),
                                            "gpu-packet-occlusion-records-final-owner-generation");
                                    occlusion.reset();
                                    Engine::OcclusionRenderer::End(commands);
                                }) != Engine::RenderResult::Failed &&
                    renderer.WaitForIdle(),
                "gpu-packet-occlusion-owner-destruction-keeps-queued-query-and-pipeline");
        Require(SUCCEEDED(renderer.GetDevice()->GetDeviceRemovedReason()), "gpu-packet-occlusion-device-remains-valid");
        if (diagnostics)
        {
            for (UINT64 index = 0; index < diagnostics->GetNumStoredMessagesAllowedByRetrievalFilter(); ++index)
            {
                SIZE_T bytes = 0;
                diagnostics->GetMessage(index, nullptr, &bytes);
                std::vector<unsigned char> storage(bytes);
                auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
                if (SUCCEEDED(diagnostics->GetMessage(index, message, &bytes)) &&
                    (message->Severity == D3D12_MESSAGE_SEVERITY_ERROR ||
                     message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION))
                    throw std::runtime_error(std::string("Occlusion validation: ") + message->pDescription);
            }
            Require(true, "gpu-packet-occlusion-has-no-debug-layer-errors");
        }
    }
    void ValidateSeparatedProjection(Engine::DirectX12Renderer& renderer, const std::filesystem::path& root,
                                     const SceneRuntime::SceneLayout& layout)
    {
        using namespace SceneRuntime;
        auto gpu = std::make_unique<SceneGpuResources>();
        auto projection = std::make_unique<EditorProjection>();
        std::string error;
        Require(gpu->Initialize(renderer, root / "Engine/Shaders/Mesh.hlsl", &error),
                "separated-resources-initialize-without-world");
        auto context = Engine::ContentContext::Capture(root);
        PreparedSceneResources empty, prepared;
        const auto view = [](const PreparedSceneResources& value)
        {
            return SceneResourceView{value.layout,    value.objects,  value.assetsRoot, value.assets,
                                     value.materials, value.animated, value.states,     value.lods};
        };
        Require(gpu->PrepareLayout(layout, root, error, false, prepared, nullptr, view(empty), true, context),
                "separated-resources-prepare-without-world-or-session");
        auto first = projection->Prepare(prepared, gpu->Device().Get(), 1, error);
        Require(first && first->objects.size() == 1 && !projection->Snapshot(),
                "separated-editor-candidate-is-not-published");
        projection->Commit(first);
        auto changed = layout;
        changed.objects.front().position[0] = 3;
        PreparedSceneResources next;
        Require(gpu->PrepareLayout(changed, root, error, true, next, &prepared, view(prepared), true, context),
                "separated-resources-prepare-edit-candidate");
        auto second = projection->Prepare(next, gpu->Device().Get(), 2, error);
        Require(second && projection->Snapshot() == first && first->objects.front().matrix._41 == 0 &&
                    second->objects.front().matrix._41 == 3,
                "separated-editor-keeps-previous-generation-until-commit");
        projection->Commit(second);
        auto invalid = changed;
        invalid.objects.front().scale[0] = 0;
        const auto retainedModel = next.objects.front().GetModel();
        Require(!gpu->PrepareLayout(invalid, root, error, true, next, &prepared, view(prepared), true, context) &&
                    projection->Snapshot() == second && next.objects.front().GetModel() == retainedModel,
                "separated-resource-failure-retains-packet-and-output");
        auto missing = changed;
        missing.objects.front().terrain.reset();
        missing.objects.front().meshRenderer.emplace();
        missing.objects.front().meshRenderer->model = "Assets/Models/Missing.glb";
        PreparedSceneResources placeholder;
        Require(gpu->PrepareLayout(missing, root, error, false, placeholder, nullptr, view(empty), true, context) &&
                    !placeholder.diagnostics.empty() && !placeholder.objects.front().GetModel(),
                "separated-editor-missing-resource-keeps-editable-placeholder");
        Require(!gpu->PrepareLayout(missing, root, error, false, placeholder, nullptr, view(empty), false, context) &&
                    projection->Snapshot() == second,
                "separated-runtime-missing-resource-rejects-candidate");
        Require(gpu->PrepareLayout(changed, root, error, true, next, &prepared, view(prepared), true, context),
                "separated-resources-retry-after-failure");
        gpu.reset();
        projection.reset();
        Engine::Camera camera;
        Engine::DirectionalLight light;
        Engine::OcclusionRenderer occlusion;
        MeshTelemetry telemetry;
        bool drawn = false;
        Require(renderer.Render({0, 0, 0, 1},
                                [&](auto* commands, float)
                                {
                                    drawn =
                                        RenderScene::Draw(second, commands, camera, light, {}, occlusion, telemetry);
                                }) != Engine::RenderResult::Failed &&
                    drawn && renderer.WaitForIdle(),
                "separated-packet-draws-after-preparation-owners-destruction");
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
        ValidateSeparatedProjection(renderer, root, layout);
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
        {
            ScriptDefinition oldDefinition, newDefinition;
            oldDefinition.update = [](ScriptContext& context)
            {
                context.object.position[0] += 1;
            };
            newDefinition.update = [](ScriptContext& context)
            {
                context.object.position[0] += 10;
            };
            Require(ScriptRegistry::InstallModule("GpuPinnedModule", {{"GpuPinned", oldDefinition}}, error),
                    "gpu-packet-installs-initial-script-generation");
            auto source = layout;
            ScriptComponent component;
            component.behaviour = "GpuPinned";
            source.objects.front().scripts = {component};
            SceneWorld pinned;
            Require(pinned.Initialize(renderer, root, source, root / "Engine/Shaders/Mesh.hlsl", &error) &&
                        ScriptRegistry::InstallModule("GpuPinnedModule", {{"GpuPinned", newDefinition}}, error) &&
                        pinned.ReplaceLayout(source, std::filesystem::relative(root), error) &&
                        pinned.StartScripts(error) && pinned.UpdateComponents(.016) &&
                        pinned.Layout().objects.front().position[0] == 1,
                    "gpu-packet-session-reset-keeps-pinned-script-definition-and-relative-root");
            Require(pinned.ReloadAssets(renderer, root, root / "Engine/Shaders/Mesh.hlsl", error) &&
                        pinned.UpdateComponents(.016) && pinned.Layout().objects.front().position[0] == 2,
                    "gpu-packet-asset-reload-keeps-session-script-generation");
            SceneWorld fresh;
            Require(fresh.Initialize(renderer, root, source, root / "Engine/Shaders/Mesh.hlsl", &error) &&
                        fresh.StartScripts(error) && fresh.UpdateComponents(.016) &&
                        fresh.Layout().objects.front().position[0] == 10,
                    "gpu-packet-new-session-captures-new-script-generation");
            Require(ScriptRegistry::InstallModule("GpuPinnedModule", {}, error),
                    "gpu-packet-removes-fixture-registry-owner");
        }

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
    void ValidateRigReload()
    {
        using namespace SceneRuntime;
        const auto root = std::filesystem::absolute("generated/tests/rig-reload") /
                          (std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
        std::filesystem::create_directories(root / "Assets/Models");
        std::filesystem::create_directories(root / "Engine");
        std::filesystem::copy("Engine/Shaders", root / "Engine/Shaders", std::filesystem::copy_options::recursive);
        const auto source = root / "Assets/Models/Actor.gltf";
        std::filesystem::copy_file(std::filesystem::path(WP1_TEST_GAME_ROOT) / "Assets/Models/AnimatedBox.gltf",
                                   source);
        std::string error;
        Engine::AssetDatabase::Ensure(source);
        const auto rig = Engine::SkeletonAssets::Load(source, error);
        if (!rig)
            throw std::runtime_error(error);
        Engine::Window window;
        Engine::DirectX12Renderer renderer;
        Require(window.Create(L"Rig reload validation", 64, 64) && renderer.Initialize(window.GetHandle()),
                "rig-reload-renderer");
        ScenePlacement actor;
        actor.id = "actor";
        actor.meshRenderer.emplace();
        actor.meshRenderer->model = "Assets/Models/Actor.gltf";
        actor.animator.emplace();
        actor.animator->initialState = "Play";
        actor.animator->states = {{"Play", rig->clips.front().name, 1, true}};
        SceneLayout layout;
        layout.objects = {actor};
        SceneWorld world;
        Require(world.Initialize(renderer, root, layout, root / "Engine/Shaders/Mesh.hlsl", &error) &&
                    world.StartScripts(error) && world.UpdateComponents(.05),
                "rig-reload-starts-shared-artifact");
        const auto old = world.RenderSnapshot();
        const auto* status = world.AnimatorStatus(actor.id);
        const auto clock = status->time;
        auto json = Engine::Json::parse(*Engine::FileContents::Read(source));
        json["nodes"][0]["name"] = "reimported-root";
        Engine::FileContents::Write(source, json.dump());
        world.TakeChanges();
        world.SetFramePreparation(
            [](auto, const auto&, const auto&, std::string& error)
            {
                error = "injected reload presentation failure";
                return false;
            });
        Require(!world.ReloadAssets(renderer, root, root / "Engine/Shaders/Mesh.hlsl", error) &&
                    world.RenderSnapshot() == old && world.AnimatorStatus(actor.id) == status &&
                    world.AnimatorStatus(actor.id)->time == clock && world.TakeChanges().Empty(),
                "rig-reload-failure-keeps-cpu-clock-gpu-generation-and-notices");
        world.SetFramePreparation({});
        Require(world.ReloadAssets(renderer, root, root / "Engine/Shaders/Mesh.hlsl", error),
                "rig-reload-retries-cpu-and-gpu-atomically");
        const auto now = world.RenderSnapshot();
        const auto oldResources =
            std::dynamic_pointer_cast<const GpuRenderObject>(old->objects.front().binding.resource);
        const auto resources = std::dynamic_pointer_cast<const GpuRenderObject>(now->objects.front().binding.resource);
        Require(resources && oldResources && resources->object.GetModel()->Rig()->nodes[0].name == "reimported-root" &&
                    oldResources->object.GetModel()->Rig()->nodes[0].name != "reimported-root" &&
                    world.AnimatorStatus(actor.id)->time == 0 && world.UpdateComponents(.05),
                "rig-reload-keeps-old-lease-and-publishes-coherent-new-pose");
        const auto beforeRename = world.RenderSnapshot();
        const auto renameClock = world.AnimatorStatus(actor.id)->time;
        Engine::AssetDatabase(root).Move("Assets/Models/Actor.gltf", "Assets/Models/Renamed.gltf");
        Require(world.ReloadAssets(renderer, root, root / "Engine/Shaders/Mesh.hlsl", error) &&
                    world.AnimatorStatus(actor.id)->time == renameClock &&
                    world.RenderSnapshot()->objects.front().entity == beforeRename->objects.front().entity &&
                    world.Layout().objects.front().meshRenderer->model == "Assets/Models/Renamed.gltf",
                "rig-rename-resolves-stable-id-and-preserves-animation-clock-and-entity");
        const auto renamedResources =
            std::dynamic_pointer_cast<const GpuRenderObject>(world.RenderSnapshot()->objects.front().binding.resource);
        Require(renamedResources &&
                    Engine::SkeletonAssets::SameArtifact(resources->object.GetModel()->Rig(),
                                                         renamedResources->object.GetModel()->Rig()) &&
                    world.UpdateComponents(.05) && world.AnimatorStatus(actor.id)->time > renameClock,
                "rig-rename-publishes-matching-cpu-gpu-rig-and-continues-animation");
        const auto stable = world.RenderSnapshot();
        Require(!world.ReplaceLayout(layout, root.parent_path(), error) && world.RenderSnapshot() == stable,
                "runtime-reset-rejects-a-different-content-owner");
        Require(renderer.WaitForIdle(), "rig-reload-completes-gpu-work");
    }
    void ValidatePresentation()
    {
        using namespace SceneRuntime;
        const auto root = std::filesystem::absolute("generated/tests/presentation-packet") /
                          (std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
        std::filesystem::create_directories(root / "Engine");
        std::filesystem::copy("Engine/Shaders", root / "Engine/Shaders", std::filesystem::copy_options::recursive);
        Engine::Window window;
        Engine::DirectX12Renderer renderer;
        Require(window.Create(L"Presentation packet validation", 64, 32) && renderer.Initialize(window.GetHandle()),
                "presentation-packet-renderer");
        window.Show();
        SceneLayout layout;
        ScenePlacement camera, sky, emitter, canvas, image;
        camera.id = "camera";
        camera.camera.emplace();
        camera.position = {0, 0, -1};
        sky.id = "sky";
        sky.sky.emplace();
        emitter.id = "emitter";
        emitter.particleEmitter.emplace();
        emitter.particleEmitter->count = 2;
        canvas.id = "canvas";
        canvas.canvas.emplace();
        canvas.canvas->referenceSize = {64, 32};
        image.id = "image";
        image.parentId = canvas.id;
        image.rectTransform.emplace();
        image.rectTransform->size = {64, 32};
        image.image.emplace();
        image.image->color = {1, 0, 0, 1};
        layout.objects = {camera, sky, emitter, canvas, image};
        layout.settings.postEffects.enabled = true;
        layout.settings.postEffects.bloomEnabled = false;
        layout.settings.postEffects.autoExposure = false;
        auto world = std::make_unique<SceneWorld>();
        auto presentation = std::make_unique<ScenePresentation>();
        std::string error;
        Require(world->Initialize(renderer, root, layout, root / "Engine/Shaders/Mesh.hlsl", &error) &&
                    presentation->Initialize(renderer, root, error, world->Content()) &&
                    presentation->Prepare(renderer, root, world->Layout(), error),
                "presentation-packet-prepares-all-effects-and-ui");
        auto frame = presentation->Capture(world->RenderSnapshot());
        Require(frame.scene && frame.scene->ui && frame.scene->ui->objects.size() == 2 && frame.resources &&
                    frame.resources->sky && frame.resources->particles && frame.resources->postEffects && frame.ui &&
                    frame.ui->parts.size() == 1,
                "presentation-packet-captures-independent-definitions-and-resource-leases");
        Engine::OcclusionRenderer occlusion;
        MeshTelemetry telemetry;
        auto bad = frame;
        auto invalid = std::make_shared<RenderPacket>(*bad.scene);
        invalid->sceneResources.reset();
        bad.scene = std::move(invalid);
        Require(renderer.Render({0, 0, 0, 1},
                                [&](auto* commands, float)
                                {
                                    if (!RenderPresentation::Draw(bad, commands, 64, 32, {}, occlusion, telemetry))
                                        throw std::runtime_error("injected packet failure after post Begin");
                                }) == Engine::RenderResult::Failed,
                "presentation-packet-failure-cancels-unsubmitted-post-pass");
        bad = {};
        Require(TestSupport::Pixel(renderer,
                                   [&](auto* commands)
                                   {
                                       Require(
                                           RenderPresentation::Draw(frame, commands, 64, 32, {}, occlusion, telemetry),
                                           "presentation-packet-retries-after-cancelled-post-pass");
                                   }) == std::array<unsigned char, 4>{255, 0, 0, 255},
                "presentation-packet-game-ui-matches-authored-pixels");
        Engine::Camera view;
        Require(SceneView::Camera(*frame.scene, 2, 0, view), "presentation-packet-camera-uses-extracted-values");
        SceneDrawOptions options;
        options.sceneCamera = &view;
        Require(TestSupport::Pixel(renderer,
                                   [&](auto* commands)
                                   {
                                       Require(RenderPresentation::Draw(frame, commands, 64, 32, options, occlusion,
                                                                        telemetry),
                                               "presentation-packet-scene-canvas-draws-from-packet");
                                   }) == std::array<unsigned char, 4>{255, 0, 0, 255},
                "presentation-packet-scene-ui-matches-authored-pixels");
        std::weak_ptr<const PresentationResources> effects = frame.resources;
        std::weak_ptr<const UiDrawResources> ui = frame.ui;
        world.reset();
        presentation.reset();
        Require(renderer.Render({0, 0, 0, 1},
                                [&](auto* commands, float)
                                {
                                    Require(RenderPresentation::Draw(frame, commands, 64, 32, {}, occlusion, telemetry),
                                            "presentation-packet-draws-after-world-and-preparer-destruction");
                                    frame = {};
                                    Require(!effects.expired() && !ui.expired(),
                                            "presentation-packet-retains-effects-and-ui-during-recording");
                                }) != Engine::RenderResult::Failed &&
                    renderer.WaitForIdle(),
                "presentation-packet-detached-submission-completes");
        for (int attempt = 0; attempt < 5 && (!effects.expired() || !ui.expired()); ++attempt)
            Require(renderer.Render({0, 0, 0, 1}) != Engine::RenderResult::Failed,
                    "presentation-packet-collects-completed-frame-slots");
        Require(effects.expired() && ui.expired(), "presentation-packet-releases-ui-and-effects-after-fence");
        Require(renderer.WaitForIdle(), "presentation-packet-gpu-completion");
    }

}
int main()
{
    try
    {
        ValidateFrames();
        ValidateWorld();
        ValidatePresentation();
        ValidateRigReload();
        std::cout << "GPU packet contracts: " << contracts << ", failures: 0\n";
        return 0;
    }
    catch (const std::exception& failure)
    {
        std::cerr << "FAIL: " << failure.what() << '\n';
        return 1;
    }
}
