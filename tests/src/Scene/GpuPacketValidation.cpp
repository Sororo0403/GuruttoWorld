#include "../../../Editor/src/Documents/DocumentSession.h"
#include <SceneRuntime/Rendering/SceneRenderState.h>
#include <SceneRuntime/Rendering/EditorProjection.h>
#include <SceneRuntime/Rendering/EditorSceneProjection.h>
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

    void ValidateRenderState(Engine::DirectX12Renderer& renderer, const std::filesystem::path& root,
                             const SceneRuntime::SceneLayout& layout)
    {
        using namespace SceneRuntime;
        size_t released = 0;
        {
            SceneRenderState state;
            PreparedSceneResources empty;
            std::shared_ptr<const RenderPacket> first(new RenderPacket{},
                                                      [&](const RenderPacket* packet)
                                                      {
                                                          delete packet;
                                                          ++released;
                                                      });
            state.CommitPrepared(empty, first, false);
            first.reset();
            auto previous = state.CommitFrame(nullptr, {}, {}, std::make_shared<RenderPacket>());
            Require(previous && released == 0 && state.Version() == 2,
                    "render-state-commit-defers-old-packet-destruction-to-caller");
            previous.reset();
            Require(released == 1, "render-state-displaced-packet-releases-after-commit");
        }
        std::shared_ptr<const RenderPacket> detached;
        {
            SceneRenderState state;
            std::string error;
            Require(state.Initialize(renderer, root / "Engine/Shaders/Mesh.hlsl", &error),
                    "render-state-initializes-without-world");
            auto context = Engine::ContentContext::Capture(root);
            PreparedSceneResources prepared;
            Require(state.Gpu().PrepareLayout(layout, root, error, false, prepared, nullptr, state.View(layout), true,
                                              context),
                    "render-state-prepares-initial-resources");
            auto authored = prepared.layout;
            auto first = state.PrepareEditor(prepared, error);
            Require(first && !state.Snapshot() && state.Read().objects.empty() && state.Version() == 0,
                    "render-state-candidate-keeps-current-storage-and-generation");
            Require(state.PrepareRetirement(prepared, error), "render-state-prepares-retirement-before-publication");
            state.CommitPrepared(prepared, first, false);
            Require(state.Snapshot() == first && state.Read().objects.size() == 1 && state.Version() == 1,
                    "render-state-publishes-storage-and-packet-together");
            Require(state.PrepareMatrices(authored, {}, error) && state.MatricesCurrent(),
                    "render-state-caches-current-generation-matrices");
            const auto oldShadow = state.Read().shadow;
            {
                auto discarded = state.Capture(authored);
                discarded.layout.settings.shadowResolution = 1024;
                Require(state.PrepareShadows(discarded, error) && state.PrepareRetirement(discarded, error) &&
                            discarded.shadow != oldShadow && state.Read().shadow == oldShadow &&
                            state.Snapshot() == first && state.Version() == 1,
                        "render-state-shadow-preparation-retains-current-generation");
            }
            Require(state.Read().shadow == oldShadow && state.Snapshot() == first && state.Version() == 1,
                    "render-state-discarded-candidate-does-not-publish");
            auto invalid = authored;
            invalid.objects.front().scale[0] = 0;
            PreparedSceneResources rejected;
            Require(!state.Gpu().PrepareLayout(invalid, root, error, true, rejected, nullptr, state.View(authored),
                                               true, context) &&
                        state.Snapshot() == first && state.Version() == 1,
                    "render-state-failed-preparation-keeps-storage-and-packet");
            auto candidate = state.Capture(authored);
            candidate.layout.objects.front().position[0] = 4;
            candidate.layout.settings.shadowResolution = 1024;
            DirectX::XMFLOAT4X4 matrix;
            DirectX::XMStoreFloat4x4(&matrix, DirectX::XMMatrixTranslation(4, 0, 0));
            Require(candidate.objects.front().SetTransform({4, 0, 0}, {}, {1, 1, 1}) &&
                        candidate.objects.front().SetWorldMatrix(matrix) && state.PrepareShadows(candidate, error),
                    "render-state-retries-pose-and-shadow-candidate");
            auto second = state.PrepareEditor(candidate, error);
            Require(second && state.PrepareRetirement(candidate, error), "render-state-prepares-coherent-next-packet");
            authored = candidate.layout;
            state.CommitPrepared(candidate, second, true);
            const auto* oldScene = dynamic_cast<const GpuRenderScene*>(first->sceneResources.get());
            const auto* newScene = dynamic_cast<const GpuRenderScene*>(second->sceneResources.get());
            Require(state.Version() == 2 && state.Snapshot() == second &&
                        state.Read().objects.front().GetPosition()[0] == 4 && !state.MatricesCurrent() &&
                        first->objects.front().matrix._41 == 0 && oldScene && newScene &&
                        oldScene->shadow == oldShadow && newScene->shadow == state.Read().shadow &&
                        newScene->shadow != oldShadow,
                    "render-state-commit-keeps-old-packet-pose-and-shadow-generation");
            Require(state.PrepareMatrices(authored, {}, error) && state.Matrices().front()._41 == 4,
                    "render-state-rebuilds-matrices-after-generation-change");
            auto sparse = state.Capture(authored);
            sparse.layout.objects.front().position[0] = 6;
            DirectX::XMStoreFloat4x4(&matrix, DirectX::XMMatrixTranslation(6, 0, 0));
            Require(sparse.objects.front().SetTransform({6, 0, 0}, {}, {1, 1, 1}) &&
                        sparse.objects.front().SetWorldMatrix(matrix),
                    "render-state-prepares-sparse-transform");
            auto sparsePacket = state.PrepareEditor(sparse, error);
            std::vector<SceneRenderTransform> transforms;
            transforms.push_back({0, {{6, 0, 0}, {}, {1, 1, 1}}, sparse.objects.front()});
            Require(sparsePacket && state.Snapshot() == second, "render-state-sparse-candidate-is-not-published");
            state.CommitFrame(nullptr, transforms, {}, sparsePacket);
            Require(state.Version() == 3 && state.Read().objects.front().GetPosition()[0] == 6 &&
                        state.Snapshot() == sparsePacket && !state.MatricesCurrent() &&
                        second->objects.front().matrix._41 == 4,
                    "render-state-sparse-frame-commits-storage-packet-and-generation");
            SceneRenderState reload;
            Require(reload.Initialize(renderer, root / "Engine/Shaders/Mesh.hlsl", &error),
                    "render-state-prepares-independent-reload-owner");
            PreparedSceneResources reloadResources;
            Require(reload.Gpu().PrepareLayout(authored, root, error, false, reloadResources, nullptr,
                                               reload.View(authored), true, context),
                    "render-state-prepares-reload-resources");
            auto reloadPacket = reload.PrepareEditor(reloadResources, error);
            Require(reloadPacket && reload.PrepareRetirement(reloadResources, error),
                    "render-state-prepares-reload-packet");
            reload.CommitPrepared(reloadResources, reloadPacket, false);
            auto reloadCandidate = reload.Capture(authored);
            Require(state.PrepareRetirement(reloadCandidate, error), "render-state-prepares-reload-retirement");
            state.CommitReload(reload, reloadPacket);
            Require(state.Version() == 4 && state.Snapshot() == reloadPacket &&
                        state.Read().objects.front().GetPosition()[0] == 4 &&
                        sparsePacket->objects.front().matrix._41 == 6,
                    "render-state-reload-exchanges-resources-without-changing-old-packet");
            detached = state.Snapshot();
        }
        Engine::Camera camera;
        Engine::DirectionalLight light;
        Engine::OcclusionRenderer occlusion;
        MeshTelemetry telemetry;
        bool drawn = false;
        Require(renderer.Render({0, 0, 0, 1},
                                [&](auto* commands, float)
                                {
                                    drawn =
                                        RenderScene::Draw(detached, commands, camera, light, {}, occlusion, telemetry);
                                }) != Engine::RenderResult::Failed &&
                    drawn && renderer.WaitForIdle(),
                "render-state-packet-outlives-state-and-reload-owner");
    }

    void ValidateEditorSceneProjection(Engine::DirectX12Renderer& renderer, const std::filesystem::path& root,
                                       const SceneRuntime::SceneLayout& layout)
    {
        using namespace SceneRuntime;
        static_assert(!std::is_base_of_v<SceneDocumentProjection, SceneWorld>);
        static_assert(std::is_base_of_v<SceneDocumentProjection, EditorSceneProjection>);
        Editor::DocumentSession session({}, Engine::ContentContext::Capture(root));
        SceneRenderState state;
        std::string error;
        Require(state.Initialize(renderer, root / "Engine/Shaders/Mesh.hlsl", &error),
                "editor-connection-initializes-render-state-without-world");
        std::shared_ptr<const RenderPacket> retained;
        {
            EditorSceneProjection projection(session.Document().Data(), state);
            Require(session.Document().Data()->Projection() == &projection &&
                        session.Edit(ReplaceDocumentEdit{layout}, error).succeeded && state.Snapshot(),
                    "editor-connection-registers-and-publishes-without-world");
            bool rejected = false;
            try
            {
                EditorSceneProjection duplicate(session.Document().Data(), state);
            }
            catch (const std::exception&)
            {
                rejected = true;
            }
            Require(rejected && session.Document().Data()->Projection() == &projection,
                    "editor-connection-rejects-duplicate-without-detaching-original");
            session.ResetHistory();
            session.Editing().Select("ground");
            session.ObserveCurrent();
            const auto before = state.Snapshot();
            Require(!projection.TakeChanges().Empty() && projection.TakeChanges().Empty(),
                    "editor-connection-publishes-notifications-once-without-world");
            auto pending = session.Prepare(TransformEdit{"ground", {{2, 0, 0}, {}, {1, 1, 1}}}, error);
            Require(pending && state.Snapshot() == before && projection.TakeChanges().Empty() &&
                        !projection.ReloadAssets(renderer, root, root / "Engine/Shaders/Mesh.hlsl", error),
                    "editor-connection-prepares-and-rejects-reload-during-document-write");
            pending.reset();
            Require(state.Snapshot() == before && !session.History().CanUndo(),
                    "editor-connection-discard-keeps-display-and-history");
            Require(session.Edit(TransformEdit{"ground", {{2, 0, 0}, {}, {1, 1, 1}}}, error).succeeded &&
                        state.Snapshot()->objects.front().matrix._41 == 2 && before->objects.front().matrix._41 == 0,
                    "editor-connection-commits-document-and-display-without-world");
            const auto stable = session.Json();
            const auto packet = state.Snapshot();
            Require(!session.Edit(NameEdit{"ground", std::string(1, static_cast<char>(0xff))}, error).succeeded &&
                        session.Json() == stable && state.Snapshot() == packet,
                    "editor-connection-history-serialization-failure-preserves-display");
            Require(session.RestoreHistory(false, error) && state.Snapshot()->objects.front().matrix._41 == 0 &&
                        session.RestoreHistory(true, error) && state.Snapshot()->objects.front().matrix._41 == 2,
                    "editor-connection-undo-redo-updates-display-without-world");
            Require(
                !projection.ReloadAssets(renderer, root / "OtherProject", root / "Engine/Shaders/Mesh.hlsl", error) &&
                    state.Snapshot()->objects.front().matrix._41 == 2 && session.Json() == stable,
                "editor-connection-rejects-resource-reload-from-another-project");
            const auto old = state.Snapshot();
            const auto undo = session.History().Target(false).json;
            Require(projection.ReloadAssets(renderer, root, root / "Engine/Shaders/Mesh.hlsl", error) &&
                        state.Snapshot() != old && state.Snapshot()->objects.front().matrix._41 == 2 &&
                        session.Json() == stable && session.History().Target(false).json == undo,
                    "editor-connection-reloads-resources-without-world-or-extra-undo");
            retained = state.Snapshot();
        }
        Require(!session.Document().Data()->Projection() && state.Snapshot() == retained &&
                    session.Edit(TransformEdit{"ground", {{3, 0, 0}, {}, {1, 1, 1}}}, error).succeeded &&
                    state.Snapshot()->objects.front().matrix._41 == 2,
                "editor-connection-detaches-and-restores-cpu-only-editing");
        {
            EditorSceneProjection reconnected(session.Document().Data(), state);
            Require(session.Edit(TransformEdit{"ground", {{3, 0, 0}, {}, {1, 1, 1}}}, error).succeeded &&
                        state.Snapshot()->objects.front().matrix._41 == 3 && retained->objects.front().matrix._41 == 2,
                    "editor-connection-reconnects-and-keeps-retained-packet");
        }
        Require(renderer.WaitForIdle(), "editor-connection-gpu-completes");
    }

    void ValidateDocumentSession(Engine::DirectX12Renderer& renderer, const std::filesystem::path& root,
                                 const SceneRuntime::SceneLayout& layout)
    {
        using namespace SceneRuntime;
        Editor::DocumentSession session({}, Engine::ContentContext::Capture(root));
        SceneWorld world(session.Document().Data());
        std::string error;
        Require(world.Initialize(renderer, root, layout, root / "Engine/Shaders/Mesh.hlsl", &error),
                "document-session-gpu-initializes-through-session");
        SceneEditPlan detached;
        detached.layout = session.Layout();
        Require(!session.Document().Data()->Projection()->PrepareDocument(detached, false, error),
                "document-session-projection-requires-document-preparation-ownership");
        session.ResetHistory();
        session.Editing().Select("ground");
        session.ObserveCurrent();
        auto before = world.RenderSnapshot();
        Require(before && world.IsDocumentView() && !world.ValidateScripts(error),
                "document-session-editing-projection-has-no-runtime-session");
        Require(world.SetLocalTransform("ground", {1, 0, 0}, {}, {1, 1, 1}) && session.History().CanUndo() &&
                    session.Layout().objects.front().position[0] == 1 &&
                    world.RenderSnapshot()->objects.front().matrix._41 == 1 && before->objects.front().matrix._41 == 0,
                "document-session-legacy-transform-publishes-document-display-and-history");
        const auto stable = session.Json();
        const auto snapshot = world.RenderSnapshot();
        const auto version = world.RenderVersion();
        Require(world.SetLocalTransform("ground", {1, 0, 0}, {}, {1, 1, 1}) && world.RenderSnapshot() == snapshot &&
                    world.RenderVersion() == version,
                "document-session-same-value-keeps-display-generation");
        world.TakeChanges();
        session.Document().Data()->TakeChanges();
        auto candidate = session.Prepare(TransformEdit{"ground", {{2, 0, 0}, {}, {1, 1, 1}}}, error);
        Require(candidate && session.Json() == stable && world.RenderSnapshot() == snapshot &&
                    !world.SetLocalTransform("ground", {9, 0, 0}, {}, {1, 1, 1}) && world.TakeChanges().Empty(),
                "document-session-preparation-retains-gpu-generation-and-rejects-world-reentry");
        candidate.reset();
        Require(world.RenderSnapshot() == snapshot && world.RenderVersion() == version &&
                    session.Document().Data()->TakeChanges().Empty(),
                "document-session-discard-keeps-gpu-generation-and-notifications");
        Require(!world.RenameObject("ground", std::string(1, static_cast<char>(0xff))) && session.Json() == stable &&
                    world.RenderSnapshot() == snapshot && world.RenderVersion() == version &&
                    world.TakeChanges().Empty() && session.Document().Data()->TakeChanges().Empty(),
                "document-session-json-failure-after-gpu-preparation-retains-document-display-and-history");
        Require(session.RestoreHistory(false, error) && world.Layout().objects.front().position[0] == 0 &&
                    world.RenderSnapshot()->objects.front().matrix._41 == 0 &&
                    session.Editing().SelectedId() == "ground",
                "document-session-gpu-undo-restores-document-display-and-selection");
        Require(session.RestoreHistory(true, error) && world.RenderSnapshot()->objects.front().matrix._41 == 1,
                "document-session-gpu-redo-restores-display");
        ScenePlacement added;
        std::string created;
        Require(world.AddObject(added, root, created, error) && !created.empty() &&
                    session.Layout().objects.size() == 2 && session.Editing().SelectedId() == created,
                "document-session-legacy-add-uses-session-id-and-selection");
        Require(session.RestoreHistory(false, error) && world.Layout().objects.size() == 1 &&
                    session.Editing().SelectedId() == "ground",
                "document-session-gpu-undo-removes-added-object");
        std::vector<std::string> copies;
        Require(world.DuplicateObjects({"ground"}, {4, 0, 0}, copies, error) && copies.size() == 1 &&
                    world.Layout().objects.size() == 2 && session.Editing().SelectedId() == copies.front(),
                "document-session-legacy-duplicate-uses-session-history");
        Require(world.RemoveObjects(copies, error) && session.Editing().SelectedIds().empty() &&
                    world.Layout().objects.size() == 1,
                "document-session-legacy-delete-clears-deleted-primary-before-history-publication");
        Require(session.RestoreHistory(false, error) && world.Layout().objects.size() == 2 &&
                    session.Editing().SelectedId() == copies.front(),
                "document-session-delete-undo-restores-display-and-primary-selection");
        Require(session.RestoreHistory(false, error) && world.Layout().objects.size() == 1,
                "document-session-gpu-undo-removes-duplicate");
        auto old = session.Prepare(TransformEdit{"ground", {{2, 0, 0}, {}, {1, 1, 1}}}, error);
        Require(old && old->Commit() && world.SetLocalTransform("ground", {3, 0, 0}, {}, {1, 1, 1}),
                "document-session-gpu-commit-releases-writer-before-old-candidate-destruction");
        auto pending = session.Prepare(TransformEdit{"ground", {{4, 0, 0}, {}, {1, 1, 1}}}, error);
        old.reset();
        Require(pending && !world.SetLocalTransform("ground", {5, 0, 0}, {}, {1, 1, 1}) &&
                    world.RenderSnapshot()->objects.front().matrix._41 == 3,
                "document-session-old-gpu-candidate-cannot-unlock-a-new-preparation");
        pending.reset();
        const auto json = session.Json();
        const auto packet = world.RenderSnapshot();
        const auto undoTarget = session.History().Target(false).json;
        world.TakeChanges();
        Require(world.ReloadAssets(renderer, root, root / "Engine/Shaders/Mesh.hlsl", error) &&
                    world.RenderSnapshot() != packet && session.Json() == json &&
                    session.History().Target(false).json == undoTarget && !world.TakeChanges().Empty(),
                "document-session-asset-reload-publishes-display-without-extra-scene-undo");
        Require(!world.SetComponentBatch({world.Layout().objects.front()}, root / "OtherProject", error) &&
                    session.Json() == json,
                "document-session-rejects-another-content-root");
        Require(renderer.WaitForIdle(), "document-session-gpu-completes");
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
        ValidateRenderState(renderer, root, layout);
        ValidateEditorSceneProjection(renderer, root, layout);
        ValidateDocumentSession(renderer, root, layout);
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
