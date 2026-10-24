#pragma once
#include <SceneRuntime/Scene/SceneWorld.h>
#include "../../../Editor/src/Documents/FileTransaction.h"
#include "../../../Editor/src/Documents/SceneDocument.h"
#include "../../../Editor/src/Documents/MaterialDocument.h"
#include <thread>
#include <type_traits>
#include <iostream>

namespace ReauditFixValidation
{
    /// <summary>再監査の成立条件を検証します。</summary>
    inline void Require(bool value, const char* message)
    {
        if (!value)
            throw std::runtime_error(message);
    }
    /// <summary>終了準備を検証する所有者を生成します。</summary>
    inline SceneRuntime::SceneLayout Actor(const std::string& behaviour)
    {
        SceneRuntime::SceneLayout layout;
        SceneRuntime::ScenePlacement object;
        object.id = "actor";
        object.scripts.push_back({"script", true, behaviour, {}});
        layout.objects.push_back(std::move(object));
        return layout;
    }
    /// <summary>終了準備後の失敗・実行可能性検証・振る舞い交換順序を検証します。</summary>
    inline void Lifecycle(Engine::DirectX12Renderer& renderer, const std::filesystem::path& root)
    {
        using namespace SceneRuntime;
        static_assert(std::is_const_v<std::remove_reference_t<decltype(std::declval<ScriptStopContext>().object)>>);
        for (int mode = 0; mode < 4; ++mode)
        {
            const auto name = "ReauditStop" + std::to_string(mode);
            bool reject = true;
            int active = 0, stops = 0;
            ScriptDefinition definition;
            definition.start = [&](ScriptContext&)
            {
                ++active;
            };
            definition.update = [](ScriptContext& context)
            {
                context.object.scripts.clear();
            };
            definition.prepareStop = [&, mode](ScriptContext& context)
            {
                context.object.position[0] += 1;
                if (reject && mode >= 2)
                {
                    context.object.scripts.push_back({"added", true, mode == 2 ? "ReauditMissing" : "Bob", {}});
                    if (mode == 3)
                        context.object.scripts.back().parameters["amplitude"] = -1;
                }
            };
            definition.stop = [&](const ScriptStopContext&)
            {
                --active;
                ++stops;
            };
            Require(ScriptRegistry::Register(name, definition), "reaudit lifecycle registration");
            SceneWorld world;
            std::string error;
            Require(world.Initialize(renderer, root, Actor(name), root / "Shaders/Mesh.hlsl", &error) &&
                        world.StartScripts(error),
                    error.c_str());
            world.SetUiPreparation(
                [&, mode](const SceneLayout& candidate, const ScriptUiCommands&, std::string& diagnostic)
                {
                    if (reject && mode < 2 && candidate.objects.front().position[0] == 1)
                    {
                        if (mode == 1)
                            throw std::runtime_error("injected post-stop UI exception");
                        diagnostic = "injected post-stop UI failure";
                        return false;
                    }
                    return true;
                });
            Require(world.UpdateFrame(.016, 0, 0, false, false), "self removal preparation");
            const auto baseline = world.Layout().Serialize();
            Require(!world.UpdateFrame(.016, 0, 0, false, false) && world.Layout().Serialize() == baseline &&
                        active == 1 && stops == 0,
                    "failed stop preparation preserves the live instance and resources");
            reject = false;
            Require(world.UpdateFrame(.016, 0, 0, false, false) && active == 0 && stops == 1 &&
                        world.Layout().objects.front().position[0] == 1 &&
                        ScriptRuntime::ValidateForPlay(world.Layout(), error),
                    "stop retry commits exactly once");
            Require(world.UpdateFrame(.016, 0, 0, false, false) && stops == 1,
                    "successful removal is not stopped twice");
        }

        bool failUpdate = false;
        int oldStops = 0;
        ScriptDefinition oldDefinition, newDefinition;
        oldDefinition.update = [](ScriptContext&) {};
        oldDefinition.prepareStop = [](ScriptContext& context)
        {
            context.object.position[0] = -1;
        };
        oldDefinition.stop = [&](const ScriptStopContext&)
        {
            ++oldStops;
        };
        newDefinition.start = [](ScriptContext& context)
        {
            Require(context.object.position[0] == -1, "old stop preparation precedes new Start");
            context.object.position[0] = 7;
        };
        newDefinition.update = [&](ScriptContext&)
        {
            if (failUpdate)
                throw std::runtime_error("new update failure");
        };
        Require(ScriptRegistry::Register("ReauditOld", oldDefinition) &&
                    ScriptRegistry::Register("ReauditNew", newDefinition),
                "replacement definitions");
        auto layout = Actor("ReauditOld");
        ScriptRuntime runtime;
        std::string error;
        Require(runtime.Update(layout, .016, error), "old behaviour starts");
        layout.objects.front().scripts.front().behaviour = "ReauditNew";
        const auto baseline = layout.Serialize();
        failUpdate = true;
        Require(!runtime.Update(layout, .016, error) && oldStops == 0 && layout.Serialize() == baseline,
                "failed replacement retains the old live instance");
        failUpdate = false;
        Require(runtime.Update(layout, .016, error) && oldStops == 1 && layout.objects.front().position[0] == 7,
                "old cleanup does not overwrite new Start");
        runtime.Stop(layout);
        Require(oldStops == 1, "replacement cleanup remains exactly once");
        std::cout << "PASS: reaudit stop preparation rollback, final validation and replacement order\n";
    }

    /// <summary>照合後の外部書込・置換・削除と、取引失敗時の復元を検証します。</summary>
    inline void Save(const std::filesystem::path& root)
    {
        const auto directory = root / "reaudit-save";
        std::filesystem::create_directories(directory);
        const auto path = directory / "document.txt";
        Engine::FileContents::Write(path, "baseline");
        {
            Engine::FileReplacement held(path, std::string("baseline"), std::string("new"));
            bool writeRejected = false, deleteRejected = false;
            try
            {
                Engine::FileContents::Write(path, "external");
            }
            catch (const std::exception&)
            {
                writeRejected = true;
            }
            try
            {
                Engine::FileContents::Remove(path);
            }
            catch (const std::exception&)
            {
                deleteRejected = true;
            }
            Require(writeRejected && deleteRejected, "held source refuses noncooperating replacement and deletion");
            held.Apply();
            Require(Engine::FileContents::Read(path) == "new",
                    "protected candidate remains readable for document validation");
            bool installedRejected = false;
            try
            {
                Engine::FileContents::Write(path, "external");
            }
            catch (const std::exception&)
            {
                installedRejected = true;
            }
            Require(installedRejected, "installed candidate stays protected through document commit");
            held.Restore();
        }
        Require(Engine::FileContents::Read(path) == "baseline", "held replacement restores original bytes");
        std::string error;
        auto plan = Editor::FileTransaction::Prepare(path, "changed");
        Require(!Editor::FileTransaction::Apply({plan}, true, error,
                                                []
                                                {
                                                    return false;
                                                }) &&
                    Engine::FileContents::Read(path) == "baseline",
                "document commit failure restores the held source");

        const auto created = directory / "created.txt";
        {
            Engine::FileReplacement pending(created, {}, std::string("candidate"));
            Engine::FileContents::Write(created, "external-created");
            bool rejected = false;
            try
            {
                pending.Apply();
            }
            catch (const std::exception&)
            {
                rejected = true;
            }
            Require(rejected && Engine::FileContents::Read(created) == "external-created",
                    "a new external destination is never overwritten");
        }
        const auto scenePath = root / "Assets/Scenes/Protected.json";
        const auto materialPath = std::filesystem::path("Assets/Materials/Protected.mat");
        SceneRuntime::SceneLayout scene;
        scene.Save(scenePath);
        SceneRuntime::MaterialAsset material;
        material.Save(root, materialPath);
        auto updatedScene = scene;
        SceneRuntime::ScenePlacement member;
        member.id = "protected";
        updatedScene.objects.push_back(member);
        material.values.roughness = .8f;
        const std::vector<Editor::FileChange> assets{
            Editor::FileTransaction::Prepare(scenePath, updatedScene.SerializeForSave(scenePath)),
            Editor::FileTransaction::Prepare(root / materialPath, material.SerializeForSave(root, materialPath))};
        Require(Editor::FileTransaction::Apply(assets, true, error,
                                               [&]
                                               {
                                                   Editor::SceneDocument document(scenePath);
                                                   Editor::MaterialDocument draft;
                                                   draft.Reload(root, materialPath);
                                                   return SceneRuntime::SceneLayout::Load(scenePath).objects.size() ==
                                                              1 &&
                                                          draft.Asset().values.roughness == .8f &&
                                                          document.Path() == scenePath;
                                               }),
                "scene, material and document readers validate protected candidates");

        // 大きい候補で、照合後に一時ファイルを観測する元の再現ケースを維持します。
        const std::string candidate(64 * 1024 * 1024, 'a');
        plan = Editor::FileTransaction::Prepare(path, candidate);
        std::atomic<bool> attempted = false, changed = false;
        std::jthread external(
            [&](std::stop_token token)
            {
                while (!token.stop_requested())
                {
                    for (const auto& file : std::filesystem::directory_iterator(directory))
                        if (file.path().extension() == ".tmp")
                        {
                            attempted = true;
                            try
                            {
                                Engine::FileContents::Write(path, "external-change");
                                changed = true;
                            }
                            catch (const std::exception&)
                            {
                            }
                            return;
                        }
                    std::this_thread::yield();
                }
            });
        const bool saved = Editor::FileTransaction::Apply({plan}, true, error,
                                                          []
                                                          {
                                                              return true;
                                                          });
        external.request_stop();
        external.join();
        const auto actual = Engine::FileContents::Read(path);
        Require(attempted && ((changed && actual == "external-change") || (!changed && saved && actual == candidate)),
                "concurrent external edit is never silently lost");
        std::cout << "PASS: reaudit held save, concurrent edit protection and rollback\n";
    }
}
