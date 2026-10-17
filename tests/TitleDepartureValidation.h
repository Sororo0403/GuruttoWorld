#pragma once
#include "../Editor/src/GameSession.h"
#include "GamePlaybackCapture.h"
#include <SceneRuntime/ProjectSettings.h>
#include <stdexcept>
#include <cmath>
#include <iostream>
namespace TitleDepartureValidation
{
    inline void Run(Engine::DirectX12Renderer& renderer, const std::filesystem::path& root)
    {
        const auto require = [](bool ok, const char* message)
        {
            if (!ok)
                throw std::runtime_error(message);
        };
        const auto layout = SceneRuntime::SceneLayout::Load(root / "Assets/Scenes/TitleStreet.json", root);
        const auto* configuration = SceneRuntime::TitleBindings::Configuration(layout);
        require(configuration != nullptr, "departure has authored menu");
        const auto start = std::find_if(configuration->entries.begin(), configuration->entries.end(),
                                        [](const auto& entry)
                                        {
                                            return entry.index == 0;
                                        });
        require(start != configuration->entries.end(), "departure has START entry");
        const auto path = std::filesystem::absolute("generated/tests/title-departure") /
                          std::to_string(GetTickCount64()) / "settings.json";
        for (const bool quit : {false, true})
        {
            Editor::GameSession session;
            std::string error;
            require(session.Play(renderer, root, layout, error, path), "departure fixture initializes");
            Engine::InputActions actions;
            actions.SetBindings(SceneRuntime::ProjectSettings::Load(root).inputActions);
            Engine::InputSnapshot snapshot;
            snapshot.active = true;
            const auto tick = [&](double dt = 1.0 / 60)
            {
                actions.Update(snapshot);
                session.PrepareInput(actions, true, dt, &snapshot);
                session.Update(dt, true);
                return session.TakeCommand();
            };
            for (int frame = 0; frame < 90; ++frame)
                tick();
            const auto cameraPose = [&]
            {
                const auto* camera = SceneRuntime::SceneView::CameraObject(session.Runtime()->World().Layout());
                DirectX::XMFLOAT4X4 matrix;
                require(camera && session.Runtime()->World().WorldMatrix(camera->id, matrix),
                        "departure camera world transform exists");
                SceneRuntime::ScenePlacement pose;
                require(SceneRuntime::SceneTransforms::ReadTransform(matrix, {}, pose),
                        "departure camera pose decomposes");
                return pose;
            };
            auto displaced = cameraPose();
            displaced.position[0] += 1;
            displaced.position[1] += .5f;
            const auto* actualCamera = SceneRuntime::SceneView::CameraObject(session.Runtime()->World().Layout());
            require(actualCamera && session.Runtime()->World().SetWorldTransform(actualCamera->id, displaced.position,
                                                                                 displaced.rotation, displaced.scale),
                    "unfinished selection camera pose fixture accepted");
            const auto before = cameraPose();
            SceneRuntime::UiEvent event;
            event.event = quit ? "menu:2" : "menu:0";
            require(session.Title().Activate(event, session.Runtime()->Ui()), "departure confirmation accepted");
            require(!tick(), "confirmation does not close or load immediately");
            require(session.Runtime()->Ui().Value("screen") == 3 &&
                        session.Runtime()->Ui().Value("departureKind") == (quit ? 1 : 0),
                    "departure hides menu and supplies correct visual kind");
            const auto initial = cameraPose();
            float firstDistance = 0;
            for (size_t axis = 0; axis < 3; ++axis)
                firstDistance +=
                    (initial.position[axis] - before.position[axis]) * (initial.position[axis] - before.position[axis]);
            require(std::sqrt(firstDistance) < .1f, "departure starts from current camera without jump");
            for (int frame = 0; frame < 20; ++frame)
                require(!tick(), "departure waits for complete camera travel");
            const float paused = session.Runtime()->Ui().Value("titleEffectTime");
            const auto pausedPose = cameraPose();
            require(session.Pause(), "departure pause accepted");
            session.Update(.1, true);
            require(session.Runtime()->Ui().Value("titleEffectTime") == paused &&
                        cameraPose().position == pausedPose.position,
                    "pause freezes departure visual and camera");
            require(session.Step(), "departure single-step accepted");
            require(session.Runtime()->Ui().Value("titleEffectTime") > paused,
                    "single-step advances departure progress");
            require(session.Play(renderer, root, layout, error, path), "departure resume accepted");
            bool paletteCaptured = false, covered = false;
            int closes = 0, loads = 0;
            for (int frame = 0; frame < 900; ++frame)
            {
                const auto command = tick();
                if (command)
                {
                    require(covered, "load or close waits until fully black dive frame was displayed");
                    closes += command->action == "quit";
                    loads += command->action == "loadScene";
                    break;
                }
                const float effectTime = session.Runtime()->Ui().Value("titleEffectTime");
                require(session.Runtime()->ActiveSceneName() == "Main", "departure stays in title until complete");
                if (effectTime >= 1 && !paletteCaptured)
                {
                    const auto mid = cameraPose();
                    require(quit ? mid.position[0] < before.position[0] : mid.position[2] > before.position[2],
                            "departure follows EXIT left arrow or START road forward direction");
                    int city = 0, exit = 0, cog = 0, buildings = 0, whiteBuildings = 0, blackBuildings = 0,
                        whiteSkyline = 0, blackSkyline = 0, roads = 0;
                    for (const auto& object : session.Runtime()->World().Layout().objects)
                    {
                        if (!object.meshRenderer || !object.material)
                            continue;
                        if (object.id == "quit-sign")
                        {
                            ++exit;
                            require(object.material->color[3] == 1 && object.material->effectAmount == 1 &&
                                        object.material->effectVariant == (quit ? 1 : 0),
                                    "EXIT lettering stays opaque white on QUIT and black on START");
                        }
                        else if (object.material->asset.filename() == "TitleBuildingEffect.mat")
                        {
                            ++buildings;
                            require(object.meshRenderer->model.filename().string().starts_with("building"),
                                    "white facade material applies only to buildings");
                            const bool white =
                                !quit &&
                                (object.id == "street-building-1" || object.id == "config-building" ||
                                 object.id == "plaza-cafe" || object.id == "skyline-0" || object.id == "skyline-2" ||
                                 object.id == "skyline-4" || object.id == "skyline-7" || object.id == "skyline-9");
                            whiteBuildings += white;
                            blackBuildings += !white;
                            if (object.id.starts_with("skyline-"))
                            {
                                whiteSkyline += white;
                                blackSkyline += !white;
                            }
                            require(
                                object.material->color[3] == 1 && object.material->effectAmount == 1 &&
                                    object.material->effectVariant == (white ? 1 : 0),
                                "START shows three foreground buildings and five selected skyline buildings white; other buildings remain black");
                        }
                        else if (object.material->asset.filename() == "TitleCityEffect.mat")
                        {
                            ++city;
                            require(object.material->color[3] == 1 && object.material->effectAmount == 1,
                                    "departure effect reaches full strength without transparency");
                            const bool isCog = object.meshRenderer->model.filename() == "cog-a.obj";
                            cog += isCog;
                            const auto model = object.meshRenderer->model.filename();
                            const bool isRoad =
                                model == "road-straight.obj" || model == "sidewalk.obj" || model == "road-bridge.obj";
                            roads += isRoad;
                            require(
                                object.material->effectVariant == (!quit && isRoad ? 1 : 0),
                                "START roads are white while ground/cog stay black; QUIT streets and cog stay black");
                        }
                    }
                    require(whiteSkyline == (quit ? 0 : 5) && blackSkyline == (quit ? 12 : 7),
                            "START adds five white skyscrapers while retaining seven black skyline buildings");
                    require(
                        city + buildings > 100 && buildings == 23 && whiteBuildings == (quit ? 0 : 8) &&
                            blackBuildings == (quit ? 23 : 15) && roads == 87 && cog > 0 && exit == 1,
                        "departure preserves exactly eight START white buildings, fifteen black buildings and eighty-seven white road meshes");
                    GamePlaybackCapture::Capture(renderer, *session.Runtime(), 1280, 720,
                                                 quit ? "quit-palette" : "start-palette");
                    paletteCaptured = true;
                }
                if (effectTime >= 2)
                {
                    const auto targetId = quit ? "quit-travel-target" : "start-travel-target";
                    DirectX::XMFLOAT4X4 marker;
                    require(session.Runtime()->World().WorldMatrix(targetId, marker),
                            "authored departure marker exists");
                    const auto pose = cameraPose();
                    require(std::abs(pose.position[0] - marker._41) < .001f &&
                                std::abs(pose.position[1] - marker._42) < .001f &&
                                std::abs(pose.position[2] - marker._43) < .001f,
                            "departure camera reaches black dive endpoint before closing or loading");
                    GamePlaybackCapture::Capture(renderer, *session.Runtime(), 1280, 720,
                                                 quit ? "quit-complete" : "start-complete", true);
                    covered = true;
                }
            }
            require(paletteCaptured && covered && closes == (quit ? 1 : 0) && loads == (quit ? 0 : 1),
                    "departure emits exactly one authored close or game load after visual completion");
            if (quit)
            {
                for (int frame = 0; frame < 20; ++frame)
                    require(!tick(), "QUIT close notification cannot repeat");
            }
            else
                require(session.Runtime()->ActiveSceneName() == start->target,
                        "START loads authored target after road travel");
            std::cout << (quit ? "QUIT" : "START") << " departure full palette/camera/paused step/completion passed\n";
            require(renderer.WaitForIdle(), "departure GPU completes");
            session.Stop();
        }
    }
}
