#pragma once
#include "SkinningValidation.h"
#include "../Editor/src/GameSession.h"
#include "../Editor/src/IkPanel.h"
#include "../Editor/src/IkHandleTransform.h"
#include "IkValidation.h"
#include <SceneRuntime/SceneEnvironment.h>

namespace IkPlaybackValidation
{
    using EnvironmentValidation::Require;
    /// <summary>インポート設定と親の姿勢を持つモデルで、IKハンドルと実際のボーン端点が一致することを確認します。</summary>
    inline void ImportedHandles(Engine::DirectX12Renderer& renderer)
    {
        using namespace Engine;
        using namespace SceneRuntime;
        using namespace DirectX;
        const auto content = std::filesystem::absolute("Games/GuruttoWorld");
        const auto root = std::filesystem::absolute("generated/tests/imported-ik-handles");
        const std::filesystem::path path = "Assets/Models/Imported.gltf";
        std::filesystem::create_directories((root / path).parent_path());
        std::ifstream source(content / "Assets/Models/IkBox.gltf");
        auto gltf = Json::parse(source);
        gltf["nodes"][0]["translation"] = {1, -2, 3};
        gltf["nodes"][0]["rotation"] = {0, std::sin(.3f), 0, std::cos(.3f)};
        {
            std::ofstream file(root / path);
            file << gltf.dump();
        }
        auto metadata = AssetDatabase::Ensure(root / path);
        metadata.scale = 2;
        AssetDatabase::Write(root / path, metadata);
        std::string error;
        const auto rig = Skeleton::Load(root / path, error);
        Require(rig != nullptr, error.c_str());
        const auto rest = Skeleton::Matrices(*rig, Skeleton::Sample(*rig, "", 0, false));
        const auto tip = std::ranges::find_if(rig->nodes,
                                              [](const auto& node)
                                              {
                                                  return node.name == "Tip";
                                              });
        Require(tip != rig->nodes.end(), "imported IK fixture has a tip");
        const auto tipIndex = static_cast<size_t>(tip - rig->nodes.begin());
        const auto point = [](const std::array<float, 3>& value, FXMMATRIX matrix)
        {
            XMFLOAT3 converted;
            XMStoreFloat3(&converted, XMVector3TransformCoord(XMVectorSet(value[0], value[1], value[2], 1), matrix));
            return std::array<float, 3>{converted.x, converted.y, converted.z};
        };
        ScenePlacement parent;
        parent.id = "parent";
        parent.position = {3, 4, 5};
        parent.scale = {1.2f, 2, .8f};
        parent.rotation = {.3f, .4f, .2f};
        ScenePlacement actor;
        actor.id = "actor";
        actor.parentId = parent.id;
        actor.position = {-.5f, .2f, 1};
        actor.rotation = {-.15f, .3f, .1f};
        actor.scale = {.9f, 1.1f, 1.4f};
        actor.SetModel(path);
        actor.animator.emplace();
        AnimatorIkConstraint constraint;
        constraint.name = "reach";
        constraint.root = "Root";
        constraint.middle = "Upper";
        constraint.tip = "Tip";
        constraint.target = point({.5f, 1.5f, 0}, XMLoadFloat4x4(&rest.front()));
        constraint.hint = point({1, 1, 1}, XMLoadFloat4x4(&rest.front()));
        actor.animator->ik = {constraint};
        SceneLayout layout;
        layout.objects = {parent, actor};
        SceneWorld world;
        Require(world.Initialize(renderer, root, layout, content / "Shaders/Mesh.hlsl", &error), error.c_str());
        XMFLOAT4X4 objectWorld, frame;
        Require(world.WorldMatrix(actor.id, objectWorld) && Editor::IkHandleTransform::Frame(world, actor.id, frame),
                "imported IK handle frame resolves");
        const auto skeletonWorld = XMLoadFloat4x4(&rig->inverseRoot) *
                                   XMMatrixScaling(rig->importScale, rig->importScale, rig->importScale) *
                                   XMLoadFloat4x4(&objectWorld);
        const auto endpoint = [&]()
        {
            const auto matrices = Skeleton::Matrices(*rig, world.AnimatorStatus(actor.id)->pose);
            const auto& m = matrices[tipIndex];
            return point({m._41, m._42, m._43}, skeletonWorld);
        };
        const auto handle = Editor::IkHandleTransform::ToWorld(constraint, frame, false);
        Require(
            handle && IkValidation::Distance(*handle, endpoint()) < .001f,
            "Editor IK handle coincides with the skinned tip after import root, import scale and parent transforms");
        const auto hint = Editor::IkHandleTransform::ToWorld(constraint, frame, true);
        Require(hint && IkValidation::Distance(*hint, point(constraint.hint, skeletonWorld)) < .001f,
                "imported local IK hint uses the same frame");
        Camera camera;
        camera.SetPosition({3, 6, -15});
        camera.SetPerspective(XM_PIDIV4, 2, .1f, 100);
        DirectionalLight light;
        light.enabled = false;
        const auto capture = [&]()
        {
            return SkinningValidation::Capture(renderer,
                                               [&](auto* commands)
                                               {
                                                   world.Draw(commands, camera, light);
                                               });
        };
        const auto before = capture();
        const auto localTarget = point({.75f, 1.25f, 0}, XMLoadFloat4x4(&rest.front()));
        const auto movedWorld = point(localTarget, skeletonWorld);
        auto changed = actor;
        Require(Editor::IkHandleTransform::Apply(changed.animator->ik[0], frame, false, movedWorld) &&
                    IkValidation::Distance(changed.animator->ik[0].target, localTarget) < .001f,
                "dragging the imported handle converts back to skeleton coordinates");
        Require(world.SetComponents(actor.id, changed, root, error) &&
                    IkValidation::Distance(endpoint(), movedWorld) < .001f,
                "edited IK tip reaches the dragged world point");
        Require(capture() != before && renderer.WaitForIdle(), "imported handle edit changes actual GPU geometry");
        auto worldConstraint = constraint;
        worldConstraint.worldSpace = true;
        worldConstraint.target = movedWorld;
        Require(Editor::IkHandleTransform::ToWorld(worldConstraint, frame, false) ==
                    std::optional<std::array<float, 3>>(movedWorld),
                "world IK handles retain authored world coordinates");
        metadata.scale = 3;
        AssetDatabase::Write(root / path, metadata);
        Require(world.ReloadAssets(renderer, root, content / "Shaders/Mesh.hlsl", error) &&
                    Editor::IkHandleTransform::Frame(world, actor.id, frame),
                "reimported IK handle uses the currently loaded model");
        const auto reimported = Editor::IkHandleTransform::ToWorld(changed.animator->ik[0], frame, false);
        const auto expected = point(localTarget, XMLoadFloat4x4(&rig->inverseRoot) * XMMatrixScaling(3, 3, 3) *
                                                     XMLoadFloat4x4(&objectWorld));
        Require(reimported && IkValidation::Distance(*reimported, expected) < .001f &&
                    IkValidation::Distance(*reimported, movedWorld) > .1f,
                "reimporting a different scale updates the handle without retaining stale import settings");
        capture();
        Require(renderer.WaitForIdle(), "reimported IK geometry completes GPU drawing");
    }
    inline void Runtime(Engine::DirectX12Renderer& renderer)
    {
        ImportedHandles(renderer);
        using namespace Engine;
        using namespace SceneRuntime;
        using namespace DirectX;
        const auto content = std::filesystem::absolute("Games/GuruttoWorld");
        std::string error;
        const auto scene = SceneLayout::Load(content / "Assets/Scenes/IkPlayground.json", content);
        const auto authored = scene.Serialize();
        Require(SceneLayout::Parse(scene.Serialize()).Serialize() == scene.Serialize(), "IK scene roundtrip");
        const auto rig = Skeleton::Load(content / "Assets/Models/IkBox.gltf", error);
        Require(rig != nullptr, error.c_str());
        const auto tip = std::ranges::find_if(rig->nodes,
                                              [](const auto& node)
                                              {
                                                  return node.name == "Tip";
                                              });
        Require(tip != rig->nodes.end(), "IK helper tip imports");
        const auto tipIndex = static_cast<size_t>(tip - rig->nodes.begin());
        SceneWorld world;
        Require(world.Initialize(renderer, content, scene, content / "Shaders/Mesh.hlsl", &error), error.c_str());
        Require(world.UpdateComponents(.1), "Script drives IK through SceneWorld");
        const auto* status = world.AnimatorStatus("Ik2");
        if (!status)
            throw std::runtime_error("IK actor status missing");
        Require(status->ikOverrides.contains("reach"), "World retains Script IK target override");
        const auto matrices = Skeleton::Matrices(*rig, status->pose);
        XMFLOAT4X4 objectWorld;
        Require(world.WorldMatrix("Ik2", objectWorld), "IK actor world transform");
        XMFLOAT3 endpoint;
        XMStoreFloat3(&endpoint,
                      XMVector3TransformCoord(
                          XMVectorSet(matrices[tipIndex]._41, matrices[tipIndex]._42, matrices[tipIndex]._43, 1),
                          XMLoadFloat4x4(&rig->inverseRoot) *
                              XMMatrixScaling(rig->importScale, rig->importScale, rig->importScale) *
                              XMLoadFloat4x4(&objectWorld)));
        const auto target = status->ikOverrides.at("reach").target;
        Require(std::abs(endpoint.x - target[0]) + std::abs(endpoint.y - target[1]) + std::abs(endpoint.z - target[2]) <
                    .001f,
                "Script-controlled tip reaches world target");
        const auto time = status->time;
        Require(world.UpdateComponents(.1) && world.AnimatorStatus("Ik2")->time > time,
                "IK target update keeps animation clock");
        ModelRenderer model;
        Require(model.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), content / "Assets/Models/IkBox.gltf",
                                 content / "Shaders/Mesh.hlsl"),
                "IK GPU model prepares");
        const auto posed = world.AnimatorStatus("Ik2")->pose;
        Require(model.ApplyPose(posed), "IK pose applies to GPU model");
        const auto paletteMatrices = Skeleton::Matrices(*model.Rig(), posed);
        std::vector<std::unique_ptr<MeshRenderer>> cpu;
        for (const auto& mesh : model.Rig()->meshes)
        {
            auto rendererMesh = std::make_unique<MeshRenderer>();
            Require(rendererMesh->Initialize(renderer.GetDevice(), renderer.GetCommandQueue(),
                                             Skeleton::Skin(*model.Rig(), mesh, paletteMatrices),
                                             content / "Shaders/Mesh.hlsl"),
                    "IK CPU oracle prepares");
            cpu.push_back(std::move(rendererMesh));
        }
        const auto bounds = model.Bounds();
        const float scale = .6f / std::max({bounds.Extents.x, bounds.Extents.y, bounds.Extents.z});
        XMFLOAT4X4 display, projection;
        XMStoreFloat4x4(&display, XMMatrixTranslation(-bounds.Center.x, -bounds.Center.y, -bounds.Center.z) *
                                      XMMatrixScaling(scale, scale, scale) * XMMatrixTranslation(0, 0, .5f));
        XMStoreFloat4x4(&projection, XMMatrixIdentity());
        DirectionalLight light;
        light.enabled = false;
        const auto expected = SkinningValidation::Capture(renderer,
                                                          [&](auto* commands)
                                                          {
                                                              for (const auto& mesh : cpu)
                                                                  mesh->Draw(commands, display, projection, light);
                                                          });
        const auto actual = SkinningValidation::Capture(renderer,
                                                        [&](auto* commands)
                                                        {
                                                            model.Draw(commands, display, projection, light);
                                                        });
        Require(actual == expected, "IK GPU image matches CPU posed geometry");
        Require(static_cast<size_t>(std::ranges::count_if(actual,
                                                          [](unsigned char value)
                                                          {
                                                              return value != 0;
                                                          })) > actual.size() / 4 + 20,
                "IK GPU image contains visible geometry");
        Require(model.ApplyPose(world.AnimatorStatus("Ik0")->pose), "zero-weight IK pose applies");
        const auto uncorrected = SkinningValidation::Capture(renderer,
                                                             [&](auto* commands)
                                                             {
                                                                 model.Draw(commands, display, projection, light);
                                                             });
        Require(uncorrected != actual, "IK weights change actual rendered geometry");
        if (!ScriptRegistry::Definitions().contains("IkPlaybackFailure"))
        {
            ScriptDefinition failure;
            failure.fields = {{"repair", {0, 0, 1}}};
            failure.update = [](ScriptContext& context)
            {
                context.state["calls"] += 1;
                context.object.name = std::to_string(context.state["calls"]);
                const auto& definition = context.object.animator->ik.front();
                AnimatorIkTarget target{definition.target, definition.hint, 1, true};
                if (context.Value("repair") == 0)
                    target.target = {-1000000, 0, 0};
                context.scene->SetIkTarget(context.object.id, definition.name, target);
            };
            Require(ScriptRegistry::Register("IkPlaybackFailure", std::move(failure)), "IK rollback driver registers");
        }
        auto failingScene = scene;
        failingScene.objects.back().scripts = {{"failure", true, "IkPlaybackFailure", {}}};
        SceneWorld transactional;
        Require(transactional.Initialize(renderer, content, failingScene, content / "Shaders/Mesh.hlsl", &error),
                error.c_str());
        const auto beforeFailure = transactional.Layout().Serialize();
        const auto beforePose = transactional.AnimatorStatus("Ik1")->pose;
        Require(!transactional.UpdateComponents(.1) && transactional.Layout().Serialize() == beforeFailure &&
                    transactional.AnimatorStatus("Ik1")->time == 0 &&
                    transactional.AnimatorStatus("Ik1")->pose[2].rotation == beforePose[2].rotation &&
                    transactional.AnimatorStatus("Ik1")->ikOverrides.empty(),
                "late IK failure preserves earlier poses, clocks, overrides and Script edits");
        auto repaired = transactional.Layout().objects.back();
        repaired.scripts[0].parameters["repair"] = 1;
        Require(transactional.SetComponents(repaired.id, repaired, content, error) &&
                    transactional.UpdateComponents(.1),
                "IK update retries after repair");
        Require(transactional.Layout().objects.back().name == "1.000000" &&
                    transactional.AnimatorStatus("Ik1")->time > 0,
                "failed IK frame does not consume Script state or time");
        SceneEnvironment app;
        Require(app.Initialize(renderer, content, scene, error), error.c_str());
        app.Update(.1, true, true);
        Require(app.World().AnimatorStatus("Ik2")->ikOverrides.contains("reach"), "App updates Script IK");
        Require(renderer.Render({0, 0, 0, 1},
                                [&](auto* commands, float)
                                {
                                    app.Draw(commands, 64, 32);
                                }) != RenderResult::Failed &&
                    renderer.WaitForIdle(),
                "App draws IK comparison");
        Editor::GameSession session;
        Require(session.Play(renderer, content, scene, error) && session.Update(.1, true) && session.Pause(),
                "Editor plays and pauses IK");
        const auto paused = session.Runtime()->World().AnimatorStatus("Ik2")->time;
        Require(!session.Update(.1, true) && session.Runtime()->World().AnimatorStatus("Ik2")->time == paused,
                "paused IK clock remains frozen");
        Require(session.Step() && session.Runtime()->World().AnimatorStatus("Ik2")->time > paused,
                "Editor steps Script IK");
        Require(renderer.Render({0, 0, 0, 1},
                                [&](auto* commands, float)
                                {
                                    session.Draw(commands, 64, 32);
                                }) != RenderResult::Failed &&
                    renderer.WaitForIdle(),
                "Editor draws IK comparison");
        Require(session.Stop() && scene.Serialize() == authored, "Editor Stop preserves authored IK targets");
#if defined(_DEBUG)
        Require(renderer.Render(
                    {0, 0, 0, 1}, [](auto*, float) {},
                    [&]
                    {
                        ImGui::Begin("IK Inspector validation");
                        auto animator = *scene.objects.back().animator;
                        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                        Editor::IkPanel::Draw(animator, rig.get());
                        ImGui::End();
                    }) != RenderResult::Failed &&
                    renderer.WaitForIdle(),
                "IK Inspector balances ImGui stack");
#endif
    }
}
