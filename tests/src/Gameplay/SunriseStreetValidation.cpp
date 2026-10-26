#include "../Support/TestGameContent.h"
#include <SceneRuntime/Scene/SceneEnvironment.h>
#include <Engine/Core/Json.h>
#include <SceneRuntime/Scripts/ScriptModuleApi.h>
#include <SceneRuntime/Gameplay/TitleMenu.h>
#include <SceneRuntime/Gameplay/GamePlayback.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <Engine/Graphics/Resources/RenderTexture.h>
#include <Engine/Platform/Window.h>
#include <Engine/Core/Log.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
bool Register_SunriseStreet(void*, SceneRuntime::RegisterModuleScript);
bool Register_StreetJourney(void*, SceneRuntime::RegisterModuleScript);
void ValidateSunriseSound();
namespace
{
    using namespace SceneRuntime;
    void Require(bool condition, const std::string& message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }
    void CameraControls()
    {
        const auto& definition = ScriptRegistry::Definitions().at("StreetCamera");
        const auto sample = [&](const std::string& field, float value)
        {
            ScenePlacement player;
            player.id = "target";
            player.position = {0, 1, 0};
            ScenePlacement camera;
            camera.id = "camera";
            camera.camera.emplace();
            SceneLayout layout;
            layout.objects = {player, camera};
            size_t next = 1;
            ScriptScene scene(layout, next);
            auto data = definition.dataFields;
            data["target"] = {ScriptObjectReference{"target"}};
            std::map<std::string, float> parameters, state;
            for (const auto& [name, setting] : definition.fields)
                parameters[name] = setting.initial;
            parameters[field] = value;
            ScriptContext context{layout.objects[1], parameters, state, 1.0 / 60, nullptr, nullptr, &scene};
            context.data = &data;
            definition.lateUpdate(context);
            return layout.objects[1];
        };
        const auto base = sample("height", 4.5f);
        Require(std::abs(base.position[0] - .5f) < .00001f && std::abs(base.position[1] - 4.4f) < .00001f &&
                    std::abs(base.position[2] + 7.6f) < .00001f,
                "Existing authored camera framing remains unchanged: " + std::to_string(base.position[0]) + "," +
                    std::to_string(base.position[1]) + "," + std::to_string(base.position[2]));
        Require(sample("height", 3).position[1] < sample("height", 6).position[1],
                "Public camera height changes framing");
        Require(sample("distance", 6).position[2] > sample("distance", 10).position[2],
                "Public camera distance changes framing");
        Require(sample("lead", 2).rotation != sample("lead", 7).rotation, "Public camera lead changes aim");
    }
    bool Register(void*, const char* name, const ScriptCallbacks* callbacks)
    {
        ScriptDefinition definition;
        static_cast<ScriptCallbacks&>(definition) = *callbacks;
        static_cast<ScriptSchema&>(definition) = ScriptSchema::LoadProject(WP1_TEST_GAME_ROOT).at(name);
        return ScriptRegistry::Register(name, std::move(definition));
    }
    ScenePlacement& Find(SceneLayout& layout, const std::string& id)
    {
        auto it = std::find_if(layout.objects.begin(), layout.objects.end(),
                               [&](const auto& o)
                               {
                                   return o.id == id;
                               });
        Require(it != layout.objects.end(), "Missing " + id);
        return *it;
    }
    bool Has(const SceneLayout& layout, const std::string& id)
    {
        return std::any_of(layout.objects.begin(), layout.objects.end(),
                           [&](const auto& o)
                           {
                               return o.id == id;
                           });
    }
    const ScenePlacement& Find(const SceneLayout& layout, const std::string& id)
    {
        auto it = std::find_if(layout.objects.begin(), layout.objects.end(),
                               [&](const auto& o)
                               {
                                   return o.id == id;
                               });
        Require(it != layout.objects.end(), "Missing " + id);
        return *it;
    }
    struct Simulation
    {
        SceneLayout layout, validated;
        PhysicsWorld physics;
        ScenePhysics::States states;
        ScriptRuntime scripts;
        UiState ui;
        int frames = 0;
        bool inspectStreet = false;
        float lastYaw = 0;
        std::array<float, 3> lastPlayer{};
        float minYaw = 10, maxYaw = -10, minFov = 180, maxFov = 0;
        explicit Simulation(const std::string& path = WP1_TEST_GAME_ROOT "/Assets/Scenes/SunriseStreet.json")
        {
            layout = SceneLayout::Load(path);
            inspectStreet = path.find("StreetJourney.json") != std::string::npos;
            if (inspectStreet && GetEnvironmentVariableA("WP1_JOURNEY_FIXED_VIEW", nullptr, 0))
                for (auto& script : Find(layout, "park-camera").scripts)
                    if (script.behaviour == "StreetCamera")
                        script.parameters["fixedView"] = 1;
            for (auto& o : layout.objects)
                for (auto& script : o.scripts)
                    if (script.behaviour == "SunriseSound")
                        script.parameters["volume"] = 0;
            layout.Validate();
            validated = layout;
        }
        ~Simulation()
        {
            scripts.Stop(layout);
        }
        void Tick(float x = 0, float z = 0, bool jump = false, bool held = false, bool attack = false)
        {
            constexpr double dt = 1.0 / 60;
            const std::map<std::string, float> input{{"MoveRight", std::max(0.0f, x)},
                                                     {"MoveLeft", std::max(0.0f, -x)},
                                                     {"MoveForward", std::max(0.0f, z)},
                                                     {"MoveBack", std::max(0.0f, -z)},
                                                     {"Jump", held ? 1.0f : 0.0f}};
            const std::map<std::string, bool> pressed{{"Jump", jump}, {"Attack", attack}};
            std::string error;
            // 検証済みの配置を基準に、製品と同じ候補準備を実行します。テストが直接加えた物理設定の変更も、
            // 最後に確定した更新段階と比較して検証します。
            const auto tickScripts = [&](ScriptPhase phase)
            {
                auto candidate = layout;
                auto runtime = scripts;
                Require(runtime.UpdateCandidate(candidate, dt, error, input, pressed, &physics, phase, &validated),
                        error);
                runtime.CommitStarts();
                layout = std::move(candidate);
                scripts = std::move(runtime);
                validated = layout;
            };
            tickScripts(ScriptPhase::FixedUpdate);
            Require(physics.Advance(layout, states, dt, x, z, jump, WP1_TEST_GAME_ROOT, error), error);
            tickScripts(ScriptPhase::Update);
            tickScripts(ScriptPhase::LateUpdate);
            if (inspectStreet)
            {
                const auto& player = Find(layout, "park-player");
                const auto& camera = Find(layout, "park-camera");
                auto delta = camera.position;
                float length = 0;
                for (size_t i = 0; i < 3; ++i)
                {
                    delta[i] -= player.position[i];
                    length += delta[i] * delta[i];
                }
                length = std::sqrt(length);
                Require(length > .25f && length < 11,
                        "Close camera distance remains finite and bounded: " + std::to_string(length) + " at frame " +
                            std::to_string(frames) + " player " + std::to_string(player.position[0]) + "," +
                            std::to_string(player.position[1]) + "," + std::to_string(player.position[2]));
                for (auto& value : delta)
                    value /= length;
                const auto hit = physics.Raycast(player.position, delta, length, 0xffffffffu, false, player.id);
                Require(!hit || hit->distance > length - .12f, "Smoothed camera does not cross a wall");
                if (frames > 0 &&
                    std::hypot(player.position[0] - lastPlayer[0], player.position[2] - lastPlayer[2]) < 6)
                    Require(std::abs(camera.rotation[1] - lastYaw) < .009f,
                            "Authored camera turns continuously during movement");
                lastPlayer = player.position;
                minYaw = std::min(minYaw, camera.rotation[1]);
                maxYaw = std::max(maxYaw, camera.rotation[1]);
                Require(camera.camera && camera.camera->verticalFov >= 50 && camera.camera->verticalFov <= 65,
                        "Bounded animated field of view");
                minFov = std::min(minFov, camera.camera->verticalFov);
                maxFov = std::max(maxFov, camera.camera->verticalFov);
                lastYaw = camera.rotation[1];
                if (GetEnvironmentVariableA("WP1_JOURNEY_FIXED_VIEW", nullptr, 0))
                {
                    Require(maxYaw - minYaw < .00001f, "Fixed view never changes yaw");
                    Require(std::abs(camera.rotation[0] - std::atan2(2.7f, std::hypot(.3f, 10.8f))) < .00001f,
                            "Fixed view never changes pitch");
                }
            }
            for (const auto& [key, value] : scripts.TakeUiCommands().values)
                ui.values[key] = value;
            ++frames;
        }
        void Walk(float x, float z, bool attack = true)
        {
            for (int t = 0; t < 1600; ++t)
            {
                const auto p = Find(layout, "park-player").position;
                if (std::hypot(x - p[0], z - p[2]) < .2f || ui.Value("parkClear") == 1)
                {
                    for (int i = 0; i < 20; ++i)
                        Tick();
                    return;
                }
                Tick(std::clamp((x - p[0]) * 3, -1.0f, 1.0f), std::clamp((z - p[2]) * 3, -1.0f, 1.0f), false, false,
                     attack && frames % 25 == 0);
            }
            const auto p = Find(layout, "park-player").position;
            throw std::runtime_error("Waypoint " + std::to_string(x) + "," + std::to_string(z) + " blocked at " +
                                     std::to_string(p[0]) + "," + std::to_string(p[1]) + "," + std::to_string(p[2]));
        }
        void JumpTo(float x, float z)
        {
            for (int t = 0; t < 240; ++t)
            {
                const auto p = Find(layout, "park-player").position;
                if (t > 20 && std::hypot(x - p[0], z - p[2]) < .22f && states.at("park-player").grounded)
                    return;
                Tick(std::clamp((x - p[0]) * 4, -1.0f, 1.0f), std::clamp((z - p[2]) * 4, -1.0f, 1.0f), t == 0, t < 42);
            }
            const auto p = Find(layout, "park-player").position;
            throw std::runtime_error("Jump to " + std::to_string(x) + "," + std::to_string(z) + " failed at " +
                                     std::to_string(p[0]) + "," + std::to_string(p[1]) + "," + std::to_string(p[2]));
        }
    };
    void Playthrough()
    {
        Simulation s;
        const auto authored = SceneLayout::Load(WP1_TEST_GAME_ROOT "/Assets/Scenes/SunriseStreet.json").Serialize();
        for (int i = 0; i < 60; ++i)
            s.Tick();
        Require(s.states.at("park-player").grounded, "Grounded on street");
        const float y = Find(s.layout, "park-player").position[1];
        s.Tick(0, 0, true, true);
        for (int i = 0; i < 30; ++i)
            s.Tick(0, 0, false, true);
        Require(Find(s.layout, "park-player").position[1] > y + .5f, "Jump rises");
        for (int i = 0; i < 130; ++i)
            s.Tick();
        s.Walk(-2, 7);
        s.Walk(2, 14);
        s.Walk(-7, 14);
        s.Walk(-7, 29);
        Require(Find(s.layout, "park-player").position[1] > 3.5f, "Roof reached through physical staircase");
        s.Walk(-9, 35);
        s.JumpTo(-11.5f, 35);
        s.JumpTo(-9, 35);
        s.Walk(-7, 38);
        s.Walk(-7, 49);
        Require(Find(s.layout, "park-player").position[1] < 1.5f, "Roof rejoins courtyard at street level");
        s.Walk(-7, 46.5f, false);
        s.Walk(7, 46.5f, false);
        s.Walk(7, 30.6f, false);
        s.Walk(7, 25, false);
        s.Walk(7, 30.6f, false);
        Require(Has(s.layout, "crate"), "Secret entrance remains closed before attacking it");
        s.Walk(15, 30.6f);
        s.Walk(15, 33);
        Require(!Has(s.layout, "crate"), "Attack opens physical secret doorway");
        Require(s.ui.Value("parkClear") == 0, "Exploration does not skip course");
        s.Walk(12, 30.6f);
        s.Walk(7, 30.6f);
        s.Walk(7, 46.5f, false);
        s.Walk(0, 46.5f, false);
        s.Walk(0, 49, false);
        s.Walk(3.8f, 46.8f, false);
        Require(Has(s.layout, "switch-canal"), "Walking alone does not activate gear");
        s.Tick(0, 0, false, false, true);
        for (int i = 0; i < 70; ++i)
            s.Tick();
        Require(!Has(s.layout, "switch-canal"), "Attack activates bridge gear");
        Require(std::abs(Find(s.layout, "canal-bridge").position[0]) < .01f, "Bridge arrives in path");
        Require(Has(s.layout, "park-gate"), "One gear keeps final gate closed");
        s.Walk(10, 50.8f);
        s.Walk(10, 52.8f);
        s.JumpTo(12, 55.3f);
        s.JumpTo(10.5f, 58);
        s.JumpTo(10, 60.2f);
        s.Walk(8, 63);
        s.Walk(0, 63);
        // 主経路の橋も往復し、どちらの向きでも通行できることを確認します。
        s.Walk(0, 49);
        s.Walk(0, 63);
        s.Walk(0, 64);
        Find(s.layout, "park-player").position[1] = -5;
        s.Tick();
        Require(Find(s.layout, "park-player").position[2] > 60, "Fall returns to canal checkpoint");
        s.Walk(4, 66);
        s.Walk(4, 70);
        s.Walk(4, 74);
        s.Walk(-4, 75);
        s.Walk(-4, 80);
        s.Walk(-3.8f, 84);
        s.Tick(0, 0, false, false, true);
        for (int i = 0; i < 40; ++i)
            s.Tick();
        Require(!Has(s.layout, "park-gate"), "Second gear opens exit");
        s.Walk(3, 90);
        s.Walk(0, 99);
        s.Walk(0, 108);
        s.Walk(0, 111);
        Require(s.ui.Value("parkClear") == 1, "Stage clears through physics play");
        const auto coins = std::count_if(s.layout.objects.begin(), s.layout.objects.end(),
                                         [](const auto& o)
                                         {
                                             return o.name == "ParkCoin";
                                         });
        for (const auto& o : s.layout.objects)
            if (o.name == "ParkCoin")
                std::cout << "Remaining " << o.id << "\n";
        Require(coins == 0, "All twelve collectibles reached");
        const auto& stats = Find(s.layout, "park-stats");
        Require(stats.text && stats.text->text.find("発見 2/2") != std::string::npos,
                "Both optional spaces discovered");
        Require(SceneLayout::Load(WP1_TEST_GAME_ROOT "/Assets/Scenes/SunriseStreet.json").Serialize() == authored,
                "Play preserves saved scene");
        std::string error;
        s.scripts.TakeSceneCommands();
        Require(s.scripts.Update(s.layout, 1.0 / 60, error, {}, {{"Restart", true}}, &s.physics), error);
        auto commands = s.scripts.TakeSceneCommands();
        Require(commands.size() == 1 && commands[0].scene == "Assets/Scenes/SunriseStreet.json",
                "Restart loads new stage");
        Require(s.scripts.Update(s.layout, 1.0 / 60, error, {}, {{"Cancel", true}}, &s.physics), error);
        commands = s.scripts.TakeSceneCommands();
        Require(commands.size() == 1 && commands[0].scene == "Assets/Scenes/TitleStreet.json",
                "Cancel returns to title");
        std::cout
            << "PASS physical playthrough: " << s.frames
            << " frames, roof stairs and ledge, secret room, canal stepping stones and bridge, 12/12, 2 discoveries, both gears, fall, clear, restart, return\n";
    }
    void DirectRoute()
    {
        Simulation s;
        for (int i = 0; i < 60; ++i)
            s.Tick();
        s.Walk(7, 14);
        s.Walk(7, 40);
        s.Walk(7, 49);
        s.Walk(3.8f, 46.8f);
        s.Tick(0, 0, false, false, true);
        for (int i = 0; i < 70; ++i)
            s.Tick();
        s.Walk(0, 49);
        s.Walk(0, 63);
        s.Walk(4, 66);
        s.Walk(4, 74);
        s.Walk(-4, 75);
        s.Walk(-3.8f, 84);
        s.Tick(0, 0, false, false, true);
        for (int i = 0; i < 40; ++i)
            s.Tick();
        s.Walk(0, 90);
        s.Walk(0, 108);
        s.Walk(0, 111);
        Require(s.ui.Value("parkClear") == 1, "Ground route clears without optional roof or room");
        Require(Has(s.layout, "flower-piece-2") && Has(s.layout, "flower-piece-5"),
                "Exploration rewards remain optional");
        Require(Find(s.layout, "park-stats").text->text.find("発見 0/2") != std::string::npos,
                "Ground route enters neither optional space");
        std::cout << "PASS direct ground route, optional room and roof skipped\n";
    }
    void BlockPlaythrough()
    {
        Simulation s(WP1_TEST_GAME_ROOT "/Assets/Scenes/SunriseBlock.json");
        for (int i = 0; i < 60; ++i)
            s.Tick();
        Require(s.states.at("park-player").grounded, "Block entrance grounded");
        const auto& camera = Find(s.layout, "park-camera");
        Require(camera.position[1] < 6 && camera.position[2] > -8, "Close camera at street level");
        const auto visibleCamera = [&]()
        {
            const auto p = Find(s.layout, "park-player").position;
            const auto cameraPosition = Find(s.layout, "park-camera").position;
            std::array<float, 3> delta{};
            float length = 0;
            for (size_t i = 0; i < 3; ++i)
            {
                delta[i] = cameraPosition[i] - p[i];
                length += delta[i] * delta[i];
            }
            length = std::sqrt(length);
            for (auto& value : delta)
                value /= length;
            const auto hit = s.physics.Raycast(p, delta, length, 0xffffffffu, false, "park-player");
            Require(!hit || hit->distance >= length - .1f, "Camera remains on player side of wall after smoothing");
        };
        s.Walk(-2.5f, 15);
        s.Walk(-4.5f, 15);
        s.Walk(0, 18);
        s.Walk(9, 20);
        visibleCamera();
        s.Walk(10.3f, 17);
        s.Walk(10.3f, 14.2f);
        s.Walk(8, 22);
        visibleCamera();
        s.Walk(11, 38);
        s.Walk(8, 43);
        visibleCamera();
        s.Walk(5, 46);
        s.Walk(11, 50);
        s.Walk(15, 54);
        Require(!Has(s.layout, "enemy-0") && !Has(s.layout, "enemy-1"), "Courtyard fight completed");
        s.Walk(7, 52.6f, false);
        Require(Has(s.layout, "switch-roof"), "Walking does not trigger rooftop mechanism");
        Require(std::abs(Find(s.layout, "roof-shutter").position[0] + 6) < .01f, "Roof shutter starts closed");
        s.Tick(0, 0, false, false, true);
        for (int i = 0; i < 75; ++i)
            s.Tick();
        Require(!Has(s.layout, "switch-roof"), "Attack operates rooftop gear");
        Require(Find(s.layout, "roof-shutter").position[0] < -10.9f, "Gear slides physical shutter open");
        s.Walk(-6, 58);
        s.Walk(-6, 71);
        s.Walk(-6, 82);
        visibleCamera();
        Require(Find(s.layout, "park-player").position[1] > 6, "Full staircase reaches high cafe roof");
        s.Walk(-7, 89);
        s.Walk(0, 91);
        visibleCamera();
        Require(s.ui.Value("parkClear") == 1, "Short block completes at rooftop view");
        for (const auto& o : s.layout.objects)
            if (o.name == "ParkCoin")
                std::cout << "Remaining block reward: " << o.id << "\n";
        Require(std::none_of(s.layout.objects.begin(), s.layout.objects.end(),
                             [](const auto& o)
                             {
                                 return o.name == "ParkCoin";
                             }),
                "All six block rewards reachable");
        std::string error;
        s.scripts.TakeSceneCommands();
        Require(s.scripts.Update(s.layout, 1.0 / 60, error, {}, {{"Restart", true}}, &s.physics), error);
        const auto commands = s.scripts.TakeSceneCommands();
        Require(commands.size() == 1 && commands[0].scene == "Assets/Scenes/SunriseBlock.json",
                "Block restart returns to same slice");
        std::cout
            << "PASS short city block: " << s.frames
            << " frames, close camera, corner, two enemies, shutter, 18 steps, roof, six rewards, clear and restart\n";
    }
    void PlazaExploration()
    {
        Simulation s(WP1_TEST_GAME_ROOT "/Assets/Scenes/SunriseBlock.json");
        for (int i = 0; i < 60; ++i)
            s.Tick();
        const auto inFrame = [&](std::array<float, 3> point, float margin)
        {
            const auto& cam = Find(s.layout, "park-camera");
            const float pitch = cam.rotation[0], yaw = cam.rotation[1];
            for (size_t i = 0; i < 3; ++i)
                point[i] -= cam.position[i];
            const float right = point[0] * std::cos(yaw) - point[2] * std::sin(yaw);
            const float up = point[0] * std::sin(yaw) * std::sin(pitch) + point[1] * std::cos(pitch) +
                             point[2] * std::cos(yaw) * std::sin(pitch);
            const float depth = point[0] * std::sin(yaw) * std::cos(pitch) - point[1] * std::sin(pitch) +
                                point[2] * std::cos(yaw) * std::cos(pitch);
            const float half = std::tan(49.0f * 3.14159265f / 360);
            Require(depth > 0 && std::abs(right) < depth * half * 16 / 9 * margin &&
                        std::abs(up) < depth * half * margin,
                    "Plaza composition keeps player / exit in frame");
        };
        const auto composition = [&](bool exit)
        {
            const auto p = Find(s.layout, "park-player").position;
            inFrame(p, .92f);
            inFrame({p[0], p[1] - .65f, p[2]}, .98f);
            if (exit)
                inFrame({8, 2.5f, 28}, .96f);
            const auto& cam = Find(s.layout, "park-camera");
            Require(std::hypot(cam.position[0] - p[0], cam.position[2] - p[2]) < 9, "Plaza retains close view");
            std::array<float, 3> d{};
            float length = 0;
            for (size_t i = 0; i < 3; ++i)
            {
                d[i] = cam.position[i] - p[i];
                length += d[i] * d[i];
            }
            length = std::sqrt(length);
            for (auto& v : d)
                v /= length;
            const auto hit = s.physics.Raycast(p, d, length, 0xffffffffu, false, "park-player");
            Require(!hit || hit->distance >= length - .1f, "Plaza detour camera is not behind wall");
        };
        // 両端への移動、花壇の左右両回り、出口からの帰路を確認します。
        // これらの検証ではテレポートを使わず、物理処理でプレイヤーを移動します。
        for (const auto& p : std::vector<std::array<float, 2>>{{-4, 7},
                                                               {-2.5f, 9},
                                                               {-2.5f, 15},
                                                               {-4, 15},
                                                               {0, 17},
                                                               {7, 1},
                                                               {7, 7},
                                                               {11.55f, 7},
                                                               {11.55f, 1},
                                                               {7, 1},
                                                               {11.55f, 1},
                                                               {11.55f, 7},
                                                               {7, 7},
                                                               {7, 17},
                                                               {11.5f, 18}})
        {
            s.Walk(p[0], p[1], false);
            composition(true);
        }
        for (const auto& p :
             std::vector<std::array<float, 2>>{{8, 24}, {8, 35}, {8, 24}, {8, 17}, {0, 17}, {-2.5f, 9}, {-4, 7}})
        {
            s.Walk(p[0], p[1], false);
            composition(false);
        }
        // 小さな切り返しではカメラの追従猶予内に収まり、画面全体が動かないことを確認します。
        // 補間区間の境界を越えても、ヨー角が急に切り替わらないことも確認します。
        s.Walk(0, 7, false);
        const float initialX = Find(s.layout, "park-camera").position[0];
        for (int i = 0; i < 5; ++i)
            s.Tick(-1, 0);
        for (int i = 0; i < 40; ++i)
            s.Tick();
        Require(std::abs(Find(s.layout, "park-camera").position[0] - initialX) < .15f,
                "Small plaza movement leaves framing stable");
        s.Walk(8, 17, false);
        float yaw = Find(s.layout, "park-camera").rotation[1];
        for (int i = 0; i < 420; ++i)
        {
            s.Tick(0, i < 210 ? 1.0f : -1.0f);
            const float next = Find(s.layout, "park-camera").rotation[1];
            Require(std::abs(next - yaw) < .06f, "Camera blend remains continuous in both directions");
            yaw = next;
        }
        std::cout
            << "PASS plaza: physical side exploration, garden loop both ways, player/feet/exit framing, close view, wall visibility, dead zone, reversible camera blend\n";
    }
    void AudioPool()
    {
        Simulation s;
        for (int i = 0; i < 30; ++i)
            s.Tick();
        const auto count = s.layout.objects.size();
        for (int repeat = 0; repeat < 8; ++repeat)
        {
            s.Tick(0, 0, false, false, true);
            s.Tick();
            s.Tick();
            Require(s.ui.Value("audio-Attack") == static_cast<float>(repeat + 1), "Preloaded cue replays successfully");
            Require(s.layout.objects.size() == count, "Sound replay never spawns or destroys objects");
            for (int i = 0; i < 30; ++i)
                s.Tick();
        }
        for (int i = 0; i < 90; ++i)
            s.Tick();
        Require(s.ui.Value("audio-Attack") == 8, "Idle cue never replays spontaneously");
        Require(s.layout.objects.size() == count, "Idle sound sources remain allocated");
        std::cout << "PASS fixed audio pool: eight replays, no object churn\n";
    }
    void Capture(Engine::DirectX12Renderer& renderer, SceneEnvironment& scene, const std::string& name)
    {
        constexpr UINT width = 1280, height = 720, pitch = width * 4;
        Engine::RenderTexture target;
        Require(target.Resize(renderer, width, height), "Capture target");
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC description{};
        description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        description.Width = static_cast<UINT64>(pitch) * height;
        description.Height = 1;
        description.DepthOrArraySize = 1;
        description.MipLevels = 1;
        description.SampleDesc.Count = 1;
        description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        Microsoft::WRL::ComPtr<ID3D12Resource> readback;
        Require(SUCCEEDED(renderer.GetDevice()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
                                                                        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                                        IID_PPV_ARGS(&readback))),
                "Capture buffer");
        Require(renderer.Render(
                    {0, 0, 0, 1},
                    [&](ID3D12GraphicsCommandList* commands, float)
                    {
                        Require(target.Begin(commands, scene.World().Layout().settings.background), "Capture begins");
                        scene.Draw(commands, width, height);
                        Require(target.End(commands), "Capture ends");
                        D3D12_RESOURCE_BARRIER barrier{};
                        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                        barrier.Transition = {target.GetResource(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                              D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                              D3D12_RESOURCE_STATE_COPY_SOURCE};
                        commands->ResourceBarrier(1, &barrier);
                        D3D12_TEXTURE_COPY_LOCATION source{};
                        source.pResource = target.GetResource();
                        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                        D3D12_TEXTURE_COPY_LOCATION destination{};
                        destination.pResource = readback.Get();
                        destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                        destination.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, pitch};
                        commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
                        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
                        commands->ResourceBarrier(1, &barrier);
                    }) != Engine::RenderResult::Failed &&
                    renderer.WaitForIdle(),
                "Capture renders");
        unsigned char* pixels = nullptr;
        const D3D12_RANGE range{0, static_cast<SIZE_T>(pitch) * height};
        Require(SUCCEEDED(readback->Map(0, &range, reinterpret_cast<void**>(&pixels))), "Capture maps");
        const auto folder = std::filesystem::path("generated/sunrise/previews");
        std::filesystem::create_directories(folder);
        std::ofstream output(folder / (name + ".ppm"), std::ios::binary);
        output << "P6\n" << width << " " << height << "\n255\n";
        for (UINT y = 0; y < height; ++y)
            for (UINT x = 0; x < width; ++x)
                output.write(reinterpret_cast<char*>(pixels + y * pitch + x * 4), 3);
        const D3D12_RANGE empty{0, 0};
        readback->Unmap(0, &empty);
        Require(static_cast<bool>(output), "Capture saved");
    }

    void Render()
    {
        Engine::Window window;
        Engine::DirectX12Renderer renderer;
        Require(window.Create(L"Sunrise street validation", 1280, 720) && renderer.Initialize(window.GetHandle()),
                "Renderer");
        const auto root = std::filesystem::absolute(WP1_TEST_GAME_ROOT);
        std::string error;
        for (const auto& [name, pose] :
             std::vector<std::pair<std::string, std::array<float, 3>>>{{"entrance", {0, 1.05f, 6}},
                                                                       {"terrace", {-7, 4, 30}},
                                                                       {"secret", {15, 1.05f, 33}},
                                                                       {"canal", {0, 1.05f, 49}},
                                                                       {"vista", {0, 1.05f, 99}}})
        {
            auto layout = SceneLayout::Load(root / "Assets/Scenes/SunriseStreet.json");
            Find(layout, "park-player").position = pose;
            Find(layout, "park-camera").position = {pose[0], 10.5f, pose[2] - 14};
            SceneEnvironment scene;
            Require(renderer.WaitForIdle() && scene.Initialize(renderer, root, layout, error), error);
            for (int i = 0; i < 90; ++i)
            {
                Require(scene.MovePlayers(1.0 / 60, 0, 0), "Fixed update");
                scene.Update(1.0 / 60, true, true);
            }
            Capture(renderer, scene, name);
            Require(renderer.WaitForIdle(), "Capture complete");
        }
        SceneEnvironment title;
        Require(title.Initialize(renderer, root, root / "Assets/Scenes/TitleStreet.json", error), error);
        auto& canvas = *Find(title.World().Layout(), "title-canvas").canvas;
        Require(canvas.menu->entries.front().target == "Assets/Scenes/StreetJourney.json", "START target");
        // App が所有するメニュー制御から返されたシーン遷移を、ホスト側で実行します。
        TitleMenu menu;
        menu.Configure(*canvas.menu);
        menu.SetTransitionDuration(3.2f);
        TitleMenuInput input;
        input.active = true;
        menu.Update(input, .1);
        const auto event = title.Click("world-start");
        menu.ActivateUi(event.event);
        bool started = false;
        for (int i = 0; i < 240; ++i)
        {
            const auto action = menu.Update(input, 1.0 / 60);
            title.SeekAnimation(3.0f, 3.0f, menu.TransitionProgress() * 3.2f);
            if (action == TitleMenuAction::Start)
            {
                Require(title.LoadScene(menu.PendingTarget(), false, error), error);
                started = true;
                break;
            }
        }
        Require(started, "Departure emits START");
        std::cout << "Title after click: " << title.ActiveSceneName() << "\n";
        Require(title.ActiveSceneName() == "Assets/Scenes/StreetJourney.json",
                "START enters the configured street journey through departure");
        Capture(renderer, title, "from-title");
        Require(renderer.WaitForIdle(), "GPU complete");
    }
    void RenderBlock()
    {
        Engine::Window window;
        Engine::DirectX12Renderer renderer;
        Require(window.Create(L"Sunrise block validation", 1280, 720) && renderer.Initialize(window.GetHandle()),
                "Block renderer");
        const auto root = std::filesystem::absolute(WP1_TEST_GAME_ROOT);
        std::string error;
        for (const auto& [name, pose] :
             std::vector<std::pair<std::string, std::array<float, 3>>>{{"block-entrance", {0, 1.05f, 1}},
                                                                       {"plaza-left", {-4, 1.05f, 15}},
                                                                       {"plaza-right", {11.5f, 1.05f, 18}},
                                                                       {"plaza-garden", {11.55f, 1.05f, 7}},
                                                                       {"block-corner", {9, 1.05f, 22}},
                                                                       {"block-courtyard", {8, 1.05f, 47}},
                                                                       {"block-stairs", {-6, 3.8f, 72}},
                                                                       {"block-roof", {-5, 6.1f, 87}}})
        {
            auto layout = SceneLayout::Load(root / "Assets/Scenes/SunriseBlock.json");
            Find(layout, "park-player").position = pose;
            Find(layout, "park-camera").position = {pose[0], pose[1] + 4.6f, pose[2] - 7.8f};
            SceneEnvironment scene;
            Require(renderer.WaitForIdle() && scene.Initialize(renderer, root, layout, error), error);
            for (int i = 0; i < 90; ++i)
            {
                Require(scene.MovePlayers(1.0 / 60, 0, 0), "Block fixed update");
                scene.Update(1.0 / 60, true, true);
            }
            Capture(renderer, scene, name);
            Require(renderer.WaitForIdle(), "Block capture complete");
        }
    }
    std::filesystem::path JourneyRoot()
    {
        std::array<char, 32768> path{};
        const auto size = GetEnvironmentVariableA("WP1_JOURNEY_CONTENT", path.data(), static_cast<DWORD>(path.size()));
        Require(size < path.size(), "Journey content path fits buffer");
        return size ? std::filesystem::path(path.data()) : std::filesystem::path(WP1_TEST_GAME_ROOT);
    }
    void JourneyPlaythrough()
    {
        const auto path = (JourneyRoot() / "Assets/Scenes/StreetJourney.json").string();
        Simulation s(path);
        for (int i = 0; i < 30; ++i)
            s.Tick();
        const auto visible = [&](std::array<float, 3> point, const std::string& name)
        {
            if (GetEnvironmentVariableA("WP1_JOURNEY_FIXED_VIEW", nullptr, 0))
                return;
            const auto& cam = Find(s.layout, "park-camera");
            std::array<float, 3> delta{};
            float length = 0;
            for (size_t i = 0; i < 3; ++i)
            {
                delta[i] = point[i] - cam.position[i];
                length += delta[i] * delta[i];
            }
            length = std::sqrt(length);
            const float pitch = cam.rotation[0], yaw = cam.rotation[1];
            const float right = delta[0] * std::cos(yaw) - delta[2] * std::sin(yaw);
            const float up = delta[0] * std::sin(yaw) * std::sin(pitch) + delta[1] * std::cos(pitch) +
                             delta[2] * std::cos(yaw) * std::sin(pitch);
            const float depth = delta[0] * std::sin(yaw) * std::cos(pitch) - delta[1] * std::sin(pitch) +
                                delta[2] * std::cos(yaw) * std::cos(pitch);
            const float half = std::tan(cam.camera->verticalFov * 3.14159265f / 360);
            Require(depth > 0 && std::abs(right) < depth * half * 16 / 9 * .95f && std::abs(up) < depth * half * .95f,
                    name + " lies inside the camera frame (x=" + std::to_string(right / (depth * half * 16 / 9)) + ")");
            for (auto& v : delta)
                v /= length;
            const auto hit = s.physics.Raycast(cam.position, delta, length, 0xffffffffu, false, "park-player");
            Require(!hit || hit->distance > length - .15f,
                    name + " has an unobstructed sightline" +
                        (hit ? "; blocked by " + hit->object + " from camera " + std::to_string(cam.position[0]) + "," +
                                   std::to_string(cam.position[1]) + "," + std::to_string(cam.position[2])
                             : ""));
            std::cout << "PASS view: " << name << "\n";
        };
        // 主な街区へ進む前に、広場の別経路も通行できることを確認します。
        s.Walk(-18.5f, 2);
        s.Walk(-3, 2);
        s.Walk(-3, 17);
        s.Walk(-18.5f, 18);
        visible({-16, 1, 23}, "quayside after circling the cafe");
        s.Walk(-15, 23);
        s.Walk(-1, 23);
        visible({3, 1.2f, 23}, "market bank before canal jump");
        s.JumpTo(3, 23);
        s.Walk(7, 26);
        s.Walk(7, 30);
        visible({1, 1.3f, 39}, "service street visible before choosing");
        visible({14, 1.3f, 39}, "public street visible from the same decision point");
        for (float x : {6.2f, 7.8f})
        {
            s.Walk(x, 30);
            visible({1, 1.3f, 39}, "service entrance from the decision area flank");
            visible({14, 1.3f, 39}, "public entrance from the decision area flank");
        }
        s.Walk(7, 30);
        // 市場棟の業務用通路を一周し、戻れることを確認します。
        s.Walk(1, 33);
        s.Walk(1, 43.5f);
        s.Walk(1, 46);
        s.Walk(8, 46);
        s.Walk(12, 46);
        s.Walk(15, 43.5f);
        s.Walk(15, 33);
        s.Walk(17, 37);
        s.Walk(15, 43.5f);
        visible({14, 4.6f, 61}, "roof stair from the market exit");
        s.Walk(12, 46);
        s.Walk(8, 49);
        {
            const auto& camera = Find(s.layout, "park-camera");
            const auto& player = Find(s.layout, "park-player");
            Require(
                std::hypot(std::hypot(camera.position[0] - player.position[0], camera.position[2] - player.position[2]),
                           camera.position[1] - player.position[1]) > 6,
                "Garden approach keeps the camera clear of the rear market pier");
        }
        s.Walk(8, 53);
        s.Walk(17, 54, false);
        Require(Has(s.layout, "journey-shutter"), "Shutter waits for deliberate interaction");
        s.Tick(0, 0, false, false, true);
        s.Tick();
        Require(!Has(s.layout, "journey-shutter"), "Gear opens the real roof stair");
        for (int i = 0; i < 180; ++i)
            s.Tick();
        const auto resting = Find(s.layout, "park-camera");
        for (int i = 0; i < 90; ++i)
            s.Tick();
        const auto stable = Find(s.layout, "park-camera");
        Require(std::hypot(stable.position[0] - resting.position[0], stable.position[2] - resting.position[2]) < .01f &&
                    std::abs(stable.position[1] - resting.position[1]) < .01f,
                "Idle camera settles");
        Find(s.layout, "park-player").position[1] = -5;
        s.Tick();
        Require(Find(s.layout, "park-player").position[2] > 47, "Fall returns to garden checkpoint");
        s.Walk(8, 53);
        s.Walk(14, 54);
        s.Walk(14, 66);
        s.Walk(14, 74);
        Require(Find(s.layout, "park-player").position[1] > 4.5f, "Twelve real stairs reach the roof");
        s.Walk(5, 74);
        visible({0, 4.2f, 74}, "garden landing before roof crossing");
        s.Walk(2.7f, 74);
        Require(Find(s.layout, "park-player").position[1] < 3.7f, "Missed jump lands on catch balcony");
        s.JumpTo(0, 74);
        s.JumpTo(5, 74);
        s.JumpTo(0, 74);
        s.Walk(0, 77.5f);
        s.JumpTo(5, 77.5f);
        s.JumpTo(0, 77.5f);
        s.Walk(0, 74);
        s.Walk(-11.5f, 71.5f);
        s.Walk(-12, 80);
        visible({-12, 4.8f, 84}, "accessible lookout beyond its entrance");
        s.Walk(-1, 80);
        s.Walk(-1, 71.5f);
        s.Walk(-11.5f, 71.5f);
        s.Walk(-12, 80);
        s.Walk(-12, 84);
        Require(s.ui.Value("journeyComplete") == 1, "Neighbourhood reaches the clock lookout");
        s.Walk(-14, 86);
        s.Walk(-10, 86);
        Require(s.maxYaw - s.minYaw < .5f, "Camera maintains a calm approach direction");
        Require(s.maxFov - s.minFov < .02f, "Field of view remains fixed");
        std::cout << "Camera yaw range " << (s.maxYaw - s.minYaw) << ", FOV " << s.minFov << ".." << s.maxFov << "\n";

        Require(s.scripts.TakeSceneCommands().empty(), "Completion preserves exploration");
        std::string error;
        s.scripts.TakeSceneCommands();
        Require(s.scripts.Update(s.layout, 1.0 / 60, error, {}, {{"Cancel", true}}, &s.physics), error);
        const auto commands = s.scripts.TakeSceneCommands();
        Require(commands.size() == 1 && commands[0].scene == "Assets/Scenes/TitleStreet.json", "Esc returns to title");
        std::cout
            << "PASS street journey: first jump, optional alley, gear/shutter, checkpoint, roof stairs, roof jump, finish; "
            << s.frames << " frames\n";
    }
    void RenderJourney()
    {
        Engine::Window window;
        Engine::DirectX12Renderer renderer;
        Require(window.Create(L"Street journey validation", 1280, 720) && renderer.Initialize(window.GetHandle()),
                "Journey renderer");
        const auto root = std::filesystem::absolute(JourneyRoot());
        std::string error;
        SceneEnvironment scene;
        Require(scene.Initialize(renderer, root, root / "Assets/Scenes/TitleStreet.json", error), error);
        GamePlayback playback;
        playback.Initialize(scene, root, std::filesystem::absolute("generated/journey/settings.json"), false);
        playback.Title().Activate(scene.Click("world-start"), scene.Ui());
        Engine::InputActions none;
        for (int i = 0; i < 330; ++i)
        {
            playback.PrepareInput(scene, none, true, 1.0 / 60);
            playback.Update(scene, 1.0 / 60, true);
        }
        Require(scene.ActiveSceneName().find("StreetJourney") != std::string::npos, "START enters street journey");
        Capture(renderer, scene, "journey-start");
        std::ifstream f(root / "Assets/Models/StreetJourney/Route.json");
        const auto metadata = Engine::Json::parse(f);
        const auto route = metadata.at("route").get<std::vector<std::array<float, 2>>>();
        int frame = 0, shot = 0;
        for (size_t waypoint = 0; waypoint < route.size(); ++waypoint)
        {
            const auto& point = route[waypoint];
            const bool jumping = std::any_of(metadata.at("jumps").begin(), metadata.at("jumps").end(),
                                             [&](const auto& jump)
                                             {
                                                 return jump.at("end").template get<std::array<float, 2>>() == point;
                                             });
            int settled = 0;
            for (int tick = 0; tick < 1400; ++tick)
            {
                const auto p = Find(scene.World().Layout(), "park-player").position;
                const float dx = point[0] - p[0], dz = point[1] - p[2];
                if (std::hypot(dx, dz) < .2f && (!jumping || tick > 30))
                {
                    if (++settled > 12)
                        break;
                }
                else
                    settled = 0;
                const float x = std::clamp(dx * 3, -1.0f, 1.0f), z = std::clamp(dz * 3, -1.0f, 1.0f);
                scene.SetInputActions({{"MoveRight", std::max(0.0f, x)},
                                       {"MoveLeft", std::max(0.0f, -x)},
                                       {"MoveForward", std::max(0.0f, z)},
                                       {"MoveBack", std::max(0.0f, -z)},
                                       {"Jump", jumping && tick < 42 ? 1.0f : 0.0f}},
                                      {{"Attack", frame % 25 == 0}, {"Jump", jumping && tick == 0}});
                Require(scene.MovePlayers(1.0 / 60, x, z, jumping && tick == 0), "Journey film physics");
                scene.Update(1.0 / 60, true, true);
                if (frame++ % 15 == 0)
                    Capture(renderer, scene, "journey-film-" + std::to_string(shot++));
                Require(tick < 1399, "Journey film waypoint " + std::to_string(waypoint));
            }
            Capture(renderer, scene, "journey-point-" + std::to_string(waypoint));
        }
        Require(scene.Ui().Value("journeyComplete") == 1, "Rendered journey reaches finish");
        // ステージ本来のカメラが静止してから、三つの休憩場所の見え方を確認します。
        for (const auto& [name, pose] :
             std::vector<std::pair<std::string, std::array<float, 3>>>{{"rest-cafe", {-12, .7f, 4.5f}},
                                                                       {"rest-garden", {8, 1.26f, 52}},
                                                                       {"rest-roof", {14, 4.62f, 74}},
                                                                       {"loading-aisle", {1, 1.26f, 36}},
                                                                       {"public-gallery", {15, 1.26f, 36}},
                                                                       {"market-choice", {7, 1.26f, 30}}})
        {
            auto layout = SceneLayout::Load(root / "Assets/Scenes/StreetJourney.json");
            Find(layout, "park-player").position = pose;
            if (name == "rest-roof")
                Find(layout, "park-player").rotation[1] = 3.14159265f;
            SceneEnvironment preview;
            Require(renderer.WaitForIdle() && preview.Initialize(renderer, root, layout, error), error);
            for (int i = 0; i < 90; ++i)
            {
                Require(preview.MovePlayers(1.0 / 60, 0, 0), "Rest area physics");
                preview.Update(1.0 / 60, true, true);
            }
            Capture(renderer, preview, name);
            Require(renderer.WaitForIdle(), "Rest area capture complete");
        }
        std::cout << "PASS rendered street journey: " << frame << " frames, " << shot << " captures\n";
    }

}
int main()
{
    std::cout.setf(std::ios::unitbuf);
    try
    {
        std::filesystem::create_directories("generated/sunrise");
        Engine::Log::Initialize("generated/sunrise/validation.log");
        Require(Register_SunriseStreet(nullptr, Register), "Scripts register");
        Require(Register_StreetJourney(nullptr, Register), "Street scripts register");
        CameraControls();
        if (GetEnvironmentVariableA("WP1_SOUND_LIFECYCLE_ONLY", nullptr, 0))
        {
            ValidateSunriseSound();
            std::cout << "PASS sound lifecycle, live clip edit, failed start, pause, step, resume and focus\n";
            return 0;
        }
        if (GetEnvironmentVariableA("WP1_JOURNEY_ONLY", nullptr, 0))
        {
            if (!GetEnvironmentVariableA("WP1_SUNRISE_RENDER_ONLY", nullptr, 0))
                JourneyPlaythrough();
            if (!GetEnvironmentVariableA("WP1_SUNRISE_CPU_ONLY", nullptr, 0))
                RenderJourney();
            std::cout << "PASS StreetJourney\n";
            return 0;
        }
        const bool blockOnly = GetEnvironmentVariableA("WP1_SUNRISE_BLOCK_ONLY", nullptr, 0) != 0;
        if (!GetEnvironmentVariableA("WP1_SUNRISE_RENDER_ONLY", nullptr, 0))
        {
            if (!blockOnly)
            {
                AudioPool();
                Playthrough();
                DirectRoute();
            }
            PlazaExploration();
            BlockPlaythrough();
        }
        if (!GetEnvironmentVariableA("WP1_SUNRISE_CPU_ONLY", nullptr, 0))
        {
            if (!blockOnly)
                Render();
            RenderBlock();
        }
        std::cout << "PASS SunriseStreet\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::ofstream("generated/street-validation-failure.txt") << e.what();
        std::cout << "FAIL: " << e.what() << "\n";
        for (const auto& log : Engine::Log::Recent())
            if (log.level == Engine::LogLevel::Error)
                std::cerr << log.text << "\n";
        return 1;
    }
}
