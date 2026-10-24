#pragma once
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include "../../../Editor/src/Playback/GameSession.h"
#include <SceneRuntime/Project/ProjectSettings.h>
#include <stdexcept>
#include <cmath>
#include <iostream>
namespace TitleConfigTransitionValidation
{
    inline void Run(Engine::DirectX12Renderer& renderer, const std::filesystem::path& root)
    {
        const auto require = [](bool ok, const char* message)
        {
            if (!ok)
                throw std::runtime_error(message);
        };
        const auto layout = SceneRuntime::SceneLayout::Load(root / "Assets/Scenes/TitleStreet.json", root);
        const auto path = std::filesystem::absolute("generated/tests/config-controls") /
                          std::to_string(GetTickCount64()) / "settings.json";
        Editor::GameSession session;
        std::string error;
        require(session.Play(renderer, root, layout, error, path), "config play initializes");
        Engine::InputActions actions;
        actions.SetBindings(SceneRuntime::ProjectSettings::Load(root).inputActions);
        Engine::InputSnapshot snapshot;
        snapshot.active = true;
        const auto tick = [&](unsigned int key = 0, double dt = .1)
        {
            snapshot.down.fill(false);
            snapshot.pressed.fill(false);
            if (key)
                snapshot.down[key] = snapshot.pressed[key] = true;
            actions.Update(snapshot);
            session.PrepareInput(actions, true, dt, &snapshot);
            session.Update(dt, true);
        };
        for (int i = 0; i < 16; ++i)
            tick();
        tick(DIK_DOWN);
        tick();
        tick(DIK_RETURN);
        require(session.Runtime()->Ui().Value("screen") == 3, "CONFIG hides controls during approach");
        const float time = session.Runtime()->Ui().Value("configEnterTime");
        require(session.Pause(), "pause accepts");
        session.Update(.1, true);
        require(session.Runtime()->Ui().Value("configEnterTime") == time, "paused presentation freezes");
        require(session.Step(), "presentation single step accepts");
        require(session.Runtime()->Ui().Value("configEnterTime") > time, "single step advances presentation");
        require(session.Play(renderer, root, layout, error, path), "resume accepts");
        for (int i = 0; i < 23; ++i)
            tick();
        require(session.Runtime()->ActiveSceneName() == "Assets/Scenes/Settings.json",
                "CONFIG loads independent Settings");
        require(session.Runtime()->Ui().Value("screen") == 1, "settings visible");
        int silhouettes = 0, cogs = 0;
        for (const auto& object : session.Runtime()->World().Layout().objects)
            if (object.meshRenderer && object.material && object.material->asset.filename() == "ConfigSilhouette.mat")
            {
                ++silhouettes;
                require(object.material->color[3] == 1, "silhouettes remain opaque");
                require(object.material->shaderParameters[0] == 1, "dedicated material effect reaches full silhouette");
            }
        for (const auto& object : session.Runtime()->World().Layout().objects)
            if (object.meshRenderer && object.material && object.material->asset.filename() == "ConfigCog.mat")
            {
                ++cogs;
                require(object.material->color[3] == 1 && object.material->shaderParameters[0] == 1 &&
                            std::abs(object.rotation[1]) > 1,
                        "white cog stays opaque and rotates");
            }
        require(silhouettes > 0 && cogs > 0, "black city and white cog fixtures exist");
        tick(DIK_RIGHT);
        tick();
        tick(DIK_DOWN);
        tick();
        tick(DIK_DOWN);
        tick();
        tick(DIK_RETURN);
        tick();
        require(std::filesystem::exists(path), "settings save uses isolated test path");
        for (int i = 0; i < 15; ++i)
            tick();
        require(session.Runtime()->ActiveSceneName() == "Assets/Scenes/TitleStreet.json", "save returns to title");
        tick(DIK_DOWN);
        tick();
        tick(DIK_RETURN);
        for (int i = 0; i < 23; ++i)
            tick();
        require(session.Runtime()->ActiveSceneName() == "Assets/Scenes/Settings.json", "repeat CONFIG enters");
        const auto before = session.Runtime()->CameraPosition();
        tick(DIK_ESCAPE, 1.0 / 60);
        const auto after = session.Runtime()->CameraPosition();
        float distance = 0;
        for (size_t axis = 0; axis < 3; ++axis)
            distance += (after[axis] - before[axis]) * (after[axis] - before[axis]);
        std::cout << "ESC first 60FPS frame camera distance " << std::sqrt(distance) << " from " << before[0] << ","
                  << before[1] << "," << before[2] << " to " << after[0] << "," << after[1] << "," << after[2] << "\n";
        require(std::sqrt(distance) < 1, "ESC return begins continuously without camera jump");
        require(session.Runtime()->Ui().Value("screen") == 3, "ESC return hides settings while moving");
        for (int i = 0; i < 12; ++i)
            tick();
        require(session.Runtime()->ActiveSceneName() == "Assets/Scenes/TitleStreet.json", "ESC return completes");
        require(renderer.WaitForIdle(), "controls GPU complete");
        session.Stop();
        require(session.Play(renderer, root, layout, error, path), "restart accepts");
        require(session.Runtime()->Ui().Value("configEnterTime") == 0, "restart resets presentation");
        snapshot = {};
        snapshot.active = true;
        snapshot.gamepadConnected = true;
        actions.Update(snapshot);
        session.PrepareInput(actions, true, .1, &snapshot);
        session.Update(.1, true);
        snapshot.buttons = snapshot.pressedButtons = XINPUT_GAMEPAD_DPAD_DOWN;
        actions.Update(snapshot);
        session.PrepareInput(actions, true, .1, &snapshot);
        session.Update(.1, true);
        require(session.Runtime()->Ui().Value("gamepad") == 1, "pad direction marks gamepad input source");
        require(renderer.WaitForIdle(), "restart GPU complete");
        session.Stop();
        auto authored = layout;
        for (auto& object : authored.objects)
        {
            if (object.cameraSway)
                object.cameraSway->enabled = false; // 視点の揺れを除いて目標姿勢を検証します。
            if (object.canvas && object.canvas->menu)
            {
                object.canvas->menu->configTransition.enterDuration = .4f;
                object.canvas->menu->configTransition.zoomDelay = 0;
                for (auto& entry : object.canvas->menu->entries)
                    if (entry.action == "settings")
                        entry.target.clear();
            }
            if (object.id == "config-camera-target")
            {
                object.parentId.clear();
                object.position = {4, 7, 12};
                object.rotation = {0, 0, 0};
            }
        }
        require(session.Play(renderer, root, authored, error, path), "authored transition fixture initializes");
        snapshot = {};
        snapshot.active = true;
        actions.SetBindings(SceneRuntime::ProjectSettings::Load(root).inputActions);
        for (int i = 0; i < 15; ++i)
            tick();
        tick(DIK_DOWN);
        tick();
        tick(DIK_RETURN);
        tick();
        require(session.Runtime()->Ui().Value("screen") == 3,
                "authored .4 second transition remains in progress before duration");
        for (int i = 0; i < 4; ++i)
            tick();
        require(session.Runtime()->Ui().Value("screen") == 1, "authored short duration completes");
        const auto target = session.Runtime()->CameraPosition();
        std::cout << "authored camera " << target[0] << "," << target[1] << "," << target[2] << " screen "
                  << session.Runtime()->Ui().Value("screen") << " time "
                  << session.Runtime()->Ui().Value("configEnterTime") << " scene "
                  << session.Runtime()->ActiveSceneName() << "\n";
        for (const auto& object : session.Runtime()->World().Layout().objects)
            if (object.id == "config-camera-target" || object.camera)
                std::cout << "pose " << object.id << " parent " << object.parentId << " p " << object.position[0] << ","
                          << object.position[1] << "," << object.position[2] << "\n";
        require(std::abs(target[0] - 4) < .001f && std::abs(target[1] - 7) < .001f && std::abs(target[2] - 12) < .001f,
                "moving authored camera marker changes transition destination");
        require(renderer.WaitForIdle(), "authored GPU complete");
        session.Stop();
        auto scripted = layout;
        SceneRuntime::ScriptDefinition definition;
        definition.update = [](SceneRuntime::ScriptContext& context)
        {
            if (context.scene && context.state["loaded"]++ == 0)
                context.scene->LoadScene("Assets/Scenes/PhysicsPlayground.json");
        };
        require(SceneRuntime::ScriptRegistry::Register("PlaybackParitySceneReplacement", definition),
                "replacement script registered");
        SceneRuntime::ScriptComponent component;
        component.behaviour = "PlaybackParitySceneReplacement";
        scripted.objects.front().scripts.push_back(component);
        require(session.Play(renderer, root, scripted, error, path), "replacement fixture initializes");
        snapshot = {};
        snapshot.active = true;
        actions.SetBindings(SceneRuntime::ProjectSettings::Load(root).inputActions);
        tick();
        require(session.Runtime()->ActiveSceneName() == "Assets/Scenes/PhysicsPlayground.json" &&
                    !session.Title().Active(),
                "internal script replacement refreshes title controller same frame");
        require(renderer.WaitForIdle(), "replacement GPU complete");
        session.Stop();
        auto replaceOnUpdate = std::make_shared<bool>(false);
        SceneRuntime::ScriptDefinition replacement;
        replacement.update = [replaceOnUpdate](SceneRuntime::ScriptContext& context)
        {
            if (*replaceOnUpdate && context.scene)
            {
                context.scene->LoadScene("Assets/Scenes/PhysicsPlayground.json");
                *replaceOnUpdate = false;
            }
        };
        require(SceneRuntime::ScriptRegistry::Register("PlaybackParitySupersededCommand", replacement),
                "superseded command script registered");
        auto race = layout;
        SceneRuntime::ScriptComponent raceScript;
        raceScript.behaviour = "PlaybackParitySupersededCommand";
        race.objects.front().scripts.push_back(raceScript);
        for (const char* eventName : {"menu:0", "menu:2"})
        {
            require(session.Play(renderer, root, race, error, path), "superseded command fixture initializes");
            snapshot = {};
            snapshot.active = true;
            actions.SetBindings(SceneRuntime::ProjectSettings::Load(root).inputActions);
            for (int i = 0; i < 15; ++i)
                tick();
            SceneRuntime::UiEvent event;
            event.event = eventName;
            session.Title().Activate(event, session.Runtime()->Ui());
            for (int i = 0; i < 8; ++i)
            {
                tick();
                require(!session.TakeCommand(), "pending transition remains un-emitted until complete covered frame");
            }
            require(session.Runtime()->ActiveSceneName() == "Main",
                    "race fixture remains in title before covered command frame");
            *replaceOnUpdate = true;
            tick();
            const auto superseded = session.TakeCommand();
            std::cout << "superseded " << eventName << " actual scene " << session.Runtime()->ActiveSceneName()
                      << " command " << (superseded ? superseded->action : "none") << "\n";
            require(session.Runtime()->ActiveSceneName() == "Assets/Scenes/PhysicsPlayground.json" && !superseded,
                    "script replacement discards previous scene pending START or QUIT");
            require(renderer.WaitForIdle(), "superseded command GPU complete");
            session.Stop();
        }
        require(session.Play(renderer, root, layout, error, path), "QUIT fixture initializes");
        snapshot = {};
        snapshot.active = true;
        actions.SetBindings(SceneRuntime::ProjectSettings::Load(root).inputActions);
        for (int i = 0; i < 15; ++i)
            tick();
        tick(DIK_DOWN);
        tick();
        tick(DIK_DOWN);
        tick();
        tick(DIK_RETURN);
        require(!session.TakeCommand(), "QUIT waits for transition before closing request");
        int quits = 0;
        for (int i = 0; i < 120; ++i)
        {
            tick();
            const auto command = session.TakeCommand();
            if (command && command->action == "quit")
                ++quits;
        }
        require(quits == 1, "QUIT emits a single close request through common playback");
        require(renderer.WaitForIdle(), "QUIT GPU complete");
        session.Stop();
    }
}
