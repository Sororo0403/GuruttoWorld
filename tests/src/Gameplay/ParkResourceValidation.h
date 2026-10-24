#pragma once
#include <SceneRuntime/Scene/SceneEnvironment.h>
#include <Engine/Core/Profiler.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ParkResourceValidation
{
    /// <summary>検証条件が満たされない場合に原因を通知します。</summary>
    inline void Require(bool value, const char* message)
    {
        if (!value)
            throw std::runtime_error(message);
    }
    /// <summary>指定IDの配置を取得し、存在しない場合は検証を失敗させます。</summary>
    inline const SceneRuntime::ScenePlacement& Find(const SceneRuntime::SceneLayout& layout, const std::string& id)
    {
        const auto found = std::find_if(layout.objects.begin(), layout.objects.end(),
                                        [&](const auto& item)
                                        {
                                            return item.id == id;
                                        });
        Require(found != layout.objects.end(), "resource regression object exists");
        return *found;
    }
    /// <summary>温めた実行環境で資源探索と描画パイプラインの再生成がないことを確認します。</summary>
    inline void NoExpensiveResources()
    {
        for (const auto& sample : Engine::CpuProfiler::Take())
            if (sample.name == "Scene asset resolution" || sample.name == "Material asset database scan" ||
                sample.name == "Asset database scan" || sample.name == "Shader compilation" ||
                sample.name == "Mesh pipeline creation" || sample.name == "Sprite pipeline creation")
                Require(sample.calls == 0, "warmed runtime mutation must reuse asset index, shaders and pipelines");
    }
    /// <summary>固定更新の描画補間、子への継承、テレポートと追いつき上限を確認します。</summary>
    inline void Interpolation(SceneRuntime::SceneEnvironment& scene)
    {
        auto& world = scene.World();
        scene.SetInputActions({}, {});
        const auto child = std::find_if(world.Layout().objects.begin(), world.Layout().objects.end(),
                                        [](const auto& item)
                                        {
                                            return item.parentId == "park-player" && item.meshRenderer;
                                        });
        Require(child != world.Layout().objects.end(), "interpolation regression player child exists");
        const auto childId = child->id;
        const auto start = Find(world.Layout(), "park-player").position;
        Require(world.MovePlayers(1.0 / 60, 0, 1), "interpolation full fixed step advances");
        const auto authoritative = Find(world.Layout(), "park-player").position;
        Require(authoritative[2] > start[2], "fixed step moves authoritative player");
        DirectX::XMFLOAT4X4 previous{}, middle{}, childMiddle{}, childAuthoritative{};
        Require(world.RenderWorldMatrix("park-player", previous), "interpolation previous pose resolves");
        Require(std::abs(previous._43 - start[2]) < 0.0001f, "zero remainder renders previous fixed pose");
        const auto saved = world.Layout().Serialize();
        Require(world.MovePlayers(1.0 / 120, 0, 1), "interpolation fractional fixed step accumulates");
        Require(world.Layout().Serialize() == saved, "fractional frame preserves authoritative scene");
        Require(world.RenderWorldMatrix("park-player", middle), "interpolation fractional render pose resolves");
        Require(std::abs(middle._43 - (start[2] + authoritative[2]) * 0.5f) < 0.0001f,
                "fractional frame renders midpoint between fixed poses");
        Require(world.RenderWorldMatrix(childId, childMiddle) && world.WorldMatrix(childId, childAuthoritative),
                "interpolation child matrices resolve");
        Require(std::abs((childAuthoritative._43 - childMiddle._43) - (authoritative[2] - middle._43)) < 0.0001f,
                "player children inherit render interpolation exactly once");
        auto teleport = authoritative;
        teleport[2] += 10;
        const auto player = Find(world.Layout(), "park-player");
        Require(world.SetWorldTransform(player.id, teleport, player.rotation, player.scale),
                "interpolation direct teleport commits");
        const auto teleported = world.Layout().Serialize();
        Require(world.RenderWorldMatrix(player.id, middle) && std::abs(middle._43 - teleport[2]) < 0.0001f,
                "direct teleport snaps instead of interpolating old history");
        Require(world.Layout().Serialize() == teleported,
                "render interpolation does not mutate authored or physics transforms");
        const auto beforeCatchup = Find(world.Layout(), "park-player");
        Require(world.MovePlayers(0.1, 0, 1), "bounded catch-up advances");
        const auto afterCatchup = Find(world.Layout(), "park-player");
        const auto movement = afterCatchup.position[2] - beforeCatchup.position[2];
        Require(movement > 0 && movement <= beforeCatchup.playerController->moveSpeed * 4.0f / 60.0f + 0.0001f,
                "catch-up performs at most four fixed movement steps");
    }
    /// <summary>物理親の回転補間と子カメラのスケールを除いた回転、直接変更時の反映を確認します。</summary>
    inline void RotationInterpolation(Engine::DirectX12Renderer& renderer)
    {
        using namespace SceneRuntime;
        const auto root = std::filesystem::absolute("Games/GuruttoWorld");
        ScenePlacement actor;
        actor.id = "rotation-parent";
        actor.position = {0, 5, 0};
        actor.scale = {2, 1, 3};
        actor.boxCollider.emplace();
        actor.boxCollider->shape = "sphere";
        actor.rigidBody.emplace();
        actor.rigidBody->gravityScale = 0;
        actor.rigidBody->angularDamping = 0;
        actor.rigidBody->angularVelocity = {0, 1, 0};
        ScenePlacement camera;
        camera.id = "rotation-camera";
        camera.parentId = actor.id;
        camera.rotation = {0.3f, 0.2f, 0};
        camera.position = {0, 1, -3};
        camera.camera.emplace();
        SceneLayout layout;
        layout.settings.mainCamera = camera.id;
        layout.objects = {actor, camera};
        SceneWorld world;
        std::string error;
        Require(world.Initialize(renderer, root, layout, root / "Shaders/Mesh.hlsl", &error),
                "rotation interpolation world initializes");
        const auto expected = [&](float yaw)
        {
            DirectX::XMFLOAT4X4 result;
            DirectX::XMStoreFloat4x4(&result, DirectX::XMMatrixRotationX(camera.rotation[0]) *
                                                  DirectX::XMMatrixRotationY(camera.rotation[1]) *
                                                  DirectX::XMMatrixRotationY(yaw));
            return result;
        };
        const auto close = [](const auto& a, const auto& b)
        {
            for (size_t row = 0; row < 4; ++row)
                for (size_t column = 0; column < 4; ++column)
                    if (std::abs(a.m[row][column] - b.m[row][column]) > 0.0003f)
                        return false;
            return true;
        };
        Require(world.MovePlayers(1.0 / 60, 0, 0), "rotating physical parent advances");
        DirectX::XMFLOAT4X4 rotation;
        Require(world.RenderWorldRotation(camera.id, rotation) && close(rotation, expected(0)),
                "child camera uses previous parent rotation at zero remainder without inherited scale");
        const auto fixed = world.Layout().Serialize();
        Require(world.MovePlayers(1.0 / 120, 0, 0), "rotating parent fractional frame accumulates");
        Require(world.RenderWorldRotation(camera.id, rotation) && close(rotation, expected(1.0f / 120)),
                "child camera parent rotation interpolates at fractional frame");
        Require(world.Layout().Serialize() == fixed, "render rotation keeps authoritative physics data unchanged");
        const auto current = Find(world.Layout(), actor.id);
        Require(world.SetLocalTransform(actor.id, current.position, {0, 0.5f, 0}, current.scale),
                "rotation-only teleport commits");
        const auto teleported = world.Layout().Serialize();
        Require(world.RenderWorldRotation(camera.id, rotation) && close(rotation, expected(0.5f)),
                "rotation-only teleport snaps camera orientation immediately");
        Require(world.Layout().Serialize() == teleported, "rotation query does not mutate teleport data");
    }
    /// <summary>実ステージのGPU使用中に資源の再利用、変更失敗の保持、描画補間を検証します。</summary>
    inline void Run(Engine::DirectX12Renderer& renderer)
    {
        using namespace SceneRuntime;
        const auto root = std::filesystem::absolute("Games/GuruttoWorld");
        auto layout = SceneLayout::Load(root / "Assets/Scenes/ParkAdventure.json", root);
        for (auto& object : layout.objects)
        {
            object.scripts.clear();
            object.rotator.reset();
            object.animation.reset();
        }
        const auto coin = std::find_if(layout.objects.begin(), layout.objects.end(),
                                       [](const auto& item)
                                       {
                                           return item.name == "ParkCoin";
                                       });
        Require(coin != layout.objects.end() && coin->meshRenderer, "resource regression coin exists");
        const std::string coinId = coin->id;
        ScriptDefinition probe;
        probe.update = [coinId](ScriptContext& context)
        {
            if (context.Pressed("Delete"))
                context.scene->Destroy(coinId);
            if (context.Pressed("Ui"))
            {
                auto text = *context.scene->Find("park-stats");
                text.text->text = "RESOURCE CACHE 123";
                context.scene->SetComponents(text.id, text);
            }
            if (context.Pressed("Clone"))
                context.scene->Instantiate(coinId, {0, 2, 8});
            if (context.Pressed("Bad"))
            {
                ScenePlacement missing;
                missing.id = "missing-resource-probe";
                missing.name = missing.id;
                missing.meshRenderer.emplace();
                missing.meshRenderer->model = "Assets/Models/MissingResourceProbe.obj";
                context.scene->Spawn(std::move(missing));
                auto text = *context.scene->Find("park-stats");
                text.text->text = "MUST ROLL BACK";
                context.scene->SetComponents(text.id, text);
            }
        };
        Require(ScriptRegistry::Register("ParkResourceProbe", std::move(probe)),
                "resource regression script registers");
        auto camera = std::find_if(layout.objects.begin(), layout.objects.end(),
                                   [](const auto& item)
                                   {
                                       return item.id == "park-camera";
                                   });
        Require(camera != layout.objects.end(), "resource regression camera exists");
        ScriptComponent component;
        component.id = "resource-probe";
        component.behaviour = "ParkResourceProbe";
        camera->scripts.push_back(component);
        SceneEnvironment scene;
        std::string error;
        Require(scene.Initialize(renderer, root, std::move(layout), error), "resource regression scene initializes");
        const auto draw = [&]()
        {
            Require(renderer.Render({0.025f, 0.035f, 0.055f, 1},
                                    [&](auto* commands, float)
                                    {
                                        scene.Draw(commands, renderer.GetWidth(), renderer.GetHeight());
                                    }) != Engine::RenderResult::Failed,
                    "resource regression renders in-flight scene");
        };
        const auto update = [&](const std::string& action)
        {
            scene.SetInputActions({}, action.empty() ? std::map<std::string, bool>{}
                                                     : std::map<std::string, bool>{{action, true}});
            scene.Update(1.0 / 60, true, true);
        };
        update("");
        draw();
        update("");
        draw();
        static_cast<void>(Engine::CpuProfiler::Take());
        const auto originalCount = scene.World().Layout().objects.size();
        update("Clone");
        draw();
        Require(scene.World().Layout().objects.size() == originalCount + 1, "runtime clone creates exactly one coin");
        NoExpensiveResources();
        update("Ui");
        draw();
        Require(Find(scene.World().Layout(), "park-stats").text->text == "RESOURCE CACHE 123",
                "runtime text mutation commits");
        NoExpensiveResources();
        // FixedUpdate は Update の前に独立して確定するため、その確定状態を基準としてリソースの変更を比較します。
        Require(scene.MovePlayers(1.0 / 60, 0, 0), "failure probe fixed phase commits before resource mutation");
        const auto saved = scene.World().Layout().Serialize();
        update("Bad");
        draw();
        Require(scene.World().Layout().Serialize() == saved,
                "missing model rejects whole mutation and text change atomically");
        static_cast<void>(Engine::CpuProfiler::Take());
        update("Delete");
        draw();
        Require(scene.World().Layout().objects.size() == originalCount,
                "runtime delete removes original coin and retains clone");
        Require(std::none_of(scene.World().Layout().objects.begin(), scene.World().Layout().objects.end(),
                             [&](const auto& item)
                             {
                                 return item.id == coinId;
                             }),
                "runtime delete removes intended coin");
        Require(Find(scene.World().Layout(), "park-stats").text->text == "RESOURCE CACHE 123",
                "failed candidate preserves existing UI resources");
        NoExpensiveResources();
        Interpolation(scene);
        draw();
        Require(renderer.WaitForIdle(), "resource regression GPU completes before environment destruction");
        RotationInterpolation(renderer);
    }
}
