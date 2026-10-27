#include <SceneRuntime/Rendering/SceneViewQueries.h>
#include <SceneRuntime/Rendering/SceneRenderState.h>
#include <SceneRuntime/Scene/SceneTransforms.h>
#include <SceneRuntime/Ui/SceneCanvas.h>
#include "../../../Editor/src/Editing/SceneSelection.h"
#include "../../../Editor/src/Editing/FocusSelection.h"
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <iostream>
#include <limits>

namespace
{
    size_t contracts = 0;
    void Require(bool valid, const char* name)
    {
        ++contracts;
        if (!valid)
            throw std::runtime_error(std::string("View query contract failed: ") + name);
        std::cout << "PASS: " << name << '\n';
    }
    void Publish(const SceneRuntime::SceneLayout& layout, SceneRuntime::SceneRenderState& state)
    {
        SceneRuntime::PreparedSceneResources prepared;
        prepared.layout = layout;
        prepared.objects.resize(layout.objects.size());
        std::string error;
        Require(SceneRuntime::SceneRenderState::PrepareTransforms(layout, prepared.objects, error),
                "view-queries-prepare-display-without-world");
        state.CommitPrepared(prepared, {}, false);
    }
}

void ValidateSceneViewQueries(Engine::DirectX12Renderer& renderer, const std::filesystem::path& root)
{
    using namespace SceneRuntime;
    SceneLayout layout;
    ScenePlacement parent, child;
    parent.id = "parent";
    parent.position = {5, 1, 0};
    parent.rotation = {0, .2f, 0};
    parent.scale = {-2, 3, 1};
    child.id = "child";
    child.parentId = parent.id;
    child.position = {1, 0, 2};
    layout.objects = {parent, child};
    SceneRenderState state;
    Publish(layout, state);
    const SceneViewQueries view(layout, state);
    const auto json = layout.Serialize();
    const auto version = state.Version();
    std::vector<DirectX::XMFLOAT4X4> expected;
    std::string error;
    Require(SceneTransforms::Resolve(layout, expected, error), "view-queries-resolve-reference-hierarchy");
    DirectX::XMFLOAT4X4 matrix;
    Require(&view.Layout() == &layout && view.WorldMatrix("child", matrix) &&
                SceneTransforms::Matches(matrix, expected[1]),
            "view-queries-read-committed-parented-mirrored-transform");
    Require(view.RenderWorldMatrix("child", matrix) && SceneTransforms::Matches(matrix, expected[1]),
            "view-queries-editor-render-matrix-matches-committed-transform");
    DirectX::XMFLOAT4X4 rotation;
    DirectX::XMStoreFloat4x4(&rotation, DirectX::XMMatrixRotationY(.2f));
    Require(view.WorldRotation("child", matrix) && SceneTransforms::Matches(matrix, rotation) &&
                view.RenderWorldRotation("child", matrix) && SceneTransforms::Matches(matrix, rotation),
            "view-queries-rotation-excludes-mirrored-parent-scale");
    ScenePlacement restored;
    Require(view.LocalTransformFromWorld("child", expected[1], restored) &&
                std::abs(restored.position[0] - 1) < 1e-5f && std::abs(restored.position[2] - 2) < 1e-5f,
            "view-queries-convert-world-transform-back-to-parent-space");
    std::array<std::array<float, 3>, 8> corners;
    Require(view.WorldBounds("child", corners), "view-queries-model-less-selection-bounds");
    Engine::Camera camera;
    camera.SetPerspective(DirectX::XM_PIDIV4, 1.5f, .1f, 1000);
    camera.SetRotation(.4f, .2f);
    const auto focused = Editor::FocusPosition(view, {"parent", "child"}, camera);
    Require(focused.has_value(), "view-queries-focus-multiple-mirrored-objects-without-world");
    camera.SetPosition(*focused);
    bool visible = true;
    for (const auto& id : {"parent", "child"})
    {
        visible = visible && view.WorldBounds(id, corners);
        for (const auto& corner : corners)
        {
            DirectX::XMFLOAT3 point;
            DirectX::XMStoreFloat3(
                &point, DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(corner[0], corner[1], corner[2], 1),
                                                         camera.GetViewMatrix() * camera.GetProjectionMatrix()));
            visible = visible && std::abs(point.x) <= 1 && std::abs(point.y) <= 1 && point.z >= 0 && point.z <= 1;
        }
    }
    Require(visible, "view-queries-focus-encloses-all-selected-corners");
    const auto savedMatrix = matrix;
    const auto savedCorners = corners;
    Require(!view.WorldMatrix("missing", matrix) && !view.RenderWorldMatrix("missing", matrix) &&
                !view.WorldRotation("missing", matrix) && !view.RenderWorldRotation("missing", matrix) &&
                !view.SkeletonWorldMatrix("child", matrix) && !view.WorldBounds("missing", corners) &&
                SceneTransforms::Matches(matrix, savedMatrix) && corners == savedCorners &&
                !Editor::FocusPosition(view, {}, camera) && !Editor::FocusPosition(view, {"missing"}, camera),
            "view-queries-missing-object-or-rig-keeps-outputs-and-rejects-focus");
    Require(layout.Serialize() == json && state.Version() == version,
            "view-queries-reads-do-not-change-document-or-render-generation");
    SceneRenderState empty;
    const SceneViewQueries unavailable(layout, empty);
    Require(!unavailable.WorldMatrix("child", matrix) && !unavailable.WorldBounds("child", corners) &&
                !unavailable.SkeletonWorldMatrix("child", matrix) && !unavailable.RenderWorldMatrix("child", matrix),
            "view-queries-unprepared-display-rejects-out-of-range-access");

    SceneLayout physics;
    parent.scale = {1, 1, 1};
    parent.position = {1, 0, 0};
    parent.rotation = {0, .4f, 0};
    parent.rigidBody.emplace();
    parent.rigidBody->motion = "dynamic";
    child.position = {0, 0, 2};
    physics.objects = {parent, child};
    SceneRenderState physical;
    Publish(physics, physical);
    std::map<std::string, SceneViewPose> previous{{"parent", {{0, 0, 0}, {}, {1, 1, 1}, {}}}};
    std::map<std::string, SceneViewPose> current{{"parent", {{1, 0, 0}, {0, .4f, 0}, {1, 1, 1}, {}}}};
    const SceneViewQueries quarter(physics, physical, {&previous, &current, .25f});
    Require(quarter.RenderWorldMatrix("parent", matrix) && std::abs(matrix._41 - .25f) < 1e-5f &&
                quarter.WorldMatrix("parent", matrix) && matrix._41 == 1,
            "view-queries-interpolate-render-pose-without-changing-committed-pose");
    DirectX::XMStoreFloat4x4(&rotation, DirectX::XMMatrixRotationY(.1f));
    Require(quarter.RenderWorldRotation("child", matrix) && SceneTransforms::Matches(matrix, rotation),
            "view-queries-inherit-interpolated-parent-rotation");
    const SceneViewQueries later(physics, physical, {&previous, &current, .75f});
    Require(later.RenderWorldMatrix("parent", matrix) && std::abs(matrix._41 - .75f) < 1e-5f &&
                quarter.RenderWorldMatrix("parent", matrix) && std::abs(matrix._41 - .25f) < 1e-5f &&
                SceneViewQueries(physics, physical).RenderWorldMatrix("parent", matrix) && matrix._41 == 1,
            "view-queries-cache-distinguishes-interpolation-and-committed-views");
    previous["parent"].position = {-4, 0, 0};
    Require(later.RenderWorldRotation("parent", matrix) &&
                SceneViewQueries(physics, physical).WorldRotation("parent", rotation) &&
                SceneTransforms::Matches(matrix, rotation),
            "view-queries-teleport-does-not-interpolate-rotation");

    SceneLayout canvasLayout;
    ScenePlacement canvas, panel;
    canvas.id = "canvas";
    canvas.position = {2, 1, 3};
    canvas.canvas.emplace();
    panel.id = "panel";
    panel.parentId = canvas.id;
    panel.rectTransform.emplace();
    canvasLayout.objects = {canvas, panel};
    SceneRenderState canvasState;
    Publish(canvasLayout, canvasState);
    const SceneViewQueries canvasView(canvasLayout, canvasState);
    Require(SceneCanvas::Matrix(canvasView, panel).has_value() &&
                Editor::FocusPosition(canvasView, {"canvas", "panel"}, camera).has_value(),
            "view-queries-canvas-conversion-and-focus-without-world");

    SceneLayout terrainLayout;
    ScenePlacement ground, top;
    ground.id = "ground";
    ground.terrain.emplace();
    ground.terrain->rows = ground.terrain->columns = 2;
    ground.terrain->heights = {0, 0, 0, 0};
    top = ground;
    top.id = "top";
    top.position = {0, 2, 0};
    top.scale = {-2, 1, 3};
    terrainLayout.objects = {ground, top};
    SceneRenderState terrain;
    Require(terrain.Initialize(renderer, root / "Engine/Shaders/Mesh.hlsl", &error),
            "view-queries-initialize-pickable-geometry-without-world");
    auto context = Engine::ContentContext::Capture(root);
    PreparedSceneResources prepared;
    Require(terrain.Gpu().PrepareLayout(terrainLayout, root, error, false, prepared, nullptr,
                                        terrain.View(terrainLayout), true, context),
            "view-queries-prepare-pickable-geometry-without-world");
    auto packet = terrain.PrepareEditor(prepared, error);
    Require(packet != nullptr, "view-queries-prepare-terrain-packet");
    terrain.CommitPrepared(prepared, std::move(packet), false);
    const SceneViewQueries picking(terrainLayout, terrain);
    Require(picking.PickRay({0, 10, 0}, {0, -2, 0}) == "top" && !picking.PickRay({0, 10, 0}, {0, -2, 0}, 7),
            "view-queries-pick-nearest-mirrored-shape-in-world-distance");
    terrainLayout.objects[1].terrain->enabled = false;
    Require(picking.PickRay({0, 10, 0}, {0, -2, 0}) == "ground", "view-queries-picking-skips-disabled-shapes");
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    Require(!picking.PickRay({nan, 0, 0}, {0, -1, 0}) && !picking.PickRay({}, {}) &&
                !picking.PickRay({}, {0, -1, 0}, -1) && !picking.PickRay({}, {nan, 0, 0}),
            "view-queries-picking-rejects-invalid-rays");
    Require(renderer.WaitForIdle(), "view-queries-gpu-completes");
    std::cout << "Scene view query contracts: " << contracts << ", failures: 0\n";
}
