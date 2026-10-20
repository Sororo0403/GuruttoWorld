#pragma once
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <SceneRuntime/SceneEnvironment.h>
#include "../Editor/src/EditHistory.h"
#include <stdexcept>
#include "FollowupStateValidation.h"

namespace SceneCollectionValidation
{
    inline void Require(bool value, const char* message)
    {
        if (!value)
            throw std::runtime_error(message);
    }
    inline SceneRuntime::SceneLayout Scene()
    {
        SceneRuntime::SceneLayout layout;
        SceneRuntime::ScenePlacement parent;
        parent.id = "parent";
        parent.name = "Parent";
        parent.position = {4, 0, 0};
        SceneRuntime::ScenePlacement child;
        child.id = "child";
        child.name = "Child";
        child.parentId = parent.id;
        child.position = {2, 0, 0};
        child.scripts.push_back({});
        child.scripts.back().behaviour = "FollowTarget";
        child.scripts.back().data["target"] = SceneRuntime::ScriptValue{SceneRuntime::ScriptObjectReference{parent.id}};
        layout.objects = {parent, child};
        return layout;
    }
    inline void Schema()
    {
        using namespace SceneRuntime;
        auto current = Scene();
        SceneCollection collection;
        collection.Reset(current, "A");
        auto merged = collection.Add(current, Scene(), "B");
        Require(merged.objects.size() == 4 && merged.objects[2].id == "parent-scene-1" &&
                    merged.objects[3].parentId == merged.objects[2].id,
                "additive IDs and parent references remap");
        const auto& reference = std::get<ScriptObjectReference>(merged.objects[3].scripts[0].data.at("target").value);
        Require(reference.id == merged.objects[2].id, "additive script references remap");
        auto cameras = current;
        cameras.objects[0].camera.emplace();
        cameras.settings.mainCamera = "parent";
        SceneCollection cameraCollection;
        cameraCollection.Reset(cameras, "Old camera");
        const auto switched = cameraCollection.Add(cameras, cameras, "New camera", true);
        Require(switched.settings.mainCamera == "parent-scene-1", "active camera follows exact additive ID remapping");
        const auto json = merged.Serialize();
        auto saved = SceneLayout::Parse(json);
        SceneCollection restored;
        restored.Reset(saved);
        Require(restored.Entries().size() == 2, "scene ownership round trips");
        Editor::EditHistory history;
        history.Reset({current.Serialize(), {}});
        history.Observe({json, {}}, {});
        Require(history.CanUndo() && SceneLayout::Parse(history.Target(false).json).objects.size() == 2,
                "additive edit is undoable");
        merged.objects[1].persistent = true;
        auto unloaded = collection.Unload(merged, "A");
        Require(unloaded.objects.size() == 3 && unloaded.objects[0].id == "child" &&
                    unloaded.objects[0].parentId.empty() && unloaded.objects[0].position[0] == 6,
                "persistent child detaches with world position");
        Require(SceneLayout::Parse(unloaded.Serialize()).objects[0].persistent, "persistent setting is saved");
        ScenePlacement spawned;
        spawned.id = "runtime-spawn";
        spawned.name = "Runtime";
        unloaded.objects.push_back(spawned);
        auto empty = collection.Unload(unloaded, "B");
        Require(empty.objects.size() == 1 && empty.objects[0].id == "child",
                "dynamic objects unload with active scene while persistent objects remain");
        bool rejected = false;
        try
        {
            collection.Unload(empty, "missing");
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        Require(rejected, "unknown scene cannot unload");
        SceneCollection generated;
        generated.Reset(current, "Owner");
        auto owned = generated.Add(current, Scene(), "Active");
        size_t nextId = 1;
        ScriptScene api(owned, nextId);
        api.currentOwner = "parent";
        const auto spawnedId = api.Spawn({});
        api.Commit();
        const auto withoutOwner = generated.Unload(owned, "Owner");
        Require(std::none_of(withoutOwner.objects.begin(), withoutOwner.objects.end(),
                             [&](const auto& object)
                             {
                                 return object.id == spawnedId;
                             }),
                "spawned roots unload with their script owner scene");
        SceneCollection reversed;
        reversed.Reset(current, "Owner");
        auto descendants = reversed.Add(current, Scene(), "Active");
        ScenePlacement grandchild;
        grandchild.id = "new-grandchild";
        grandchild.parentId = "new-child";
        ScenePlacement newChild;
        newChild.id = "new-child";
        newChild.parentId = "parent";
        descendants.objects.insert(descendants.objects.begin(), {grandchild, newChild});
        auto removeOwner = reversed;
        const auto withoutAncestor = removeOwner.Unload(descendants, "Owner");
        Require(withoutAncestor.objects.size() == 2 && withoutAncestor.objects[0].id == "parent-scene-1",
                "all new descendants unload with their ancestor scene regardless of object order");
        const auto withoutActive = reversed.Unload(descendants, "Active");
        Require(withoutActive.objects.size() == 4 && withoutActive.objects[0].parentId == "new-child",
                "new descendants inherit their ancestor scene regardless of object order");
        Require(withoutActive.sceneObjects.at("Owner").size() == 4,
                "saved ownership includes every inherited descendant");
        SceneCollection savedOwner;
        savedOwner.Reset(current, "Owner");
        auto explicitOwner = savedOwner.Add(current, Scene(), "Active");
        newChild.parentId.clear();
        explicitOwner.objects.insert(explicitOwner.objects.begin(), {grandchild, newChild});
        explicitOwner.sceneObjects["Owner"].push_back(newChild.id);
        Require(savedOwner.Unload(explicitOwner, "Owner").objects.size() == 2,
                "saved ownership is restored before descendants inherit an otherwise unowned parent");
    }
    inline void Runtime(Engine::DirectX12Renderer& renderer)
    {
        FollowupStateValidation::EditorOwnership(renderer);
        using namespace SceneRuntime;
        const auto root = std::filesystem::absolute("generated/tests/scene-collection");
        std::filesystem::create_directories(root / "Assets/Scenes");
        std::filesystem::create_directories(root / "Shaders");
        for (const auto& [relative, source] : Engine::ContentFiles::List("Games/GuruttoWorld"))
            if (relative.generic_string().starts_with("Shaders/"))
            {
                const auto target = root / relative;
                std::filesystem::create_directories(target.parent_path());
                std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing);
            }
        auto stopped = std::make_shared<int>(0);
        ScriptDefinition definition;
        definition.update = [](ScriptContext& c)
        {
            c.object.position[2] = ++c.state["ticks"];
        };
        definition.stop = [stopped](const ScriptStopContext&)
        {
            ++*stopped;
        };
        Require(ScriptRegistry::Register("ValidationPersistentScene", definition), "persistent runtime definition");
        auto initial = Scene();
        initial.objects[1].scripts.clear();
        initial.objects[0].persistent = true;
        initial.objects[0].button.emplace();
        initial.objects[0].button->action = "loadSceneAdditive";
        initial.objects[0].button->target = "Assets/Scenes/B.json";
        ScriptComponent script;
        script.behaviour = "ValidationPersistentScene";
        initial.objects[0].scripts.push_back(script);
        initial.Save(root / "Assets/Scenes/A.json");
        auto other = Scene();
        other.objects[1].scripts.clear();
        other.objects[0].scripts.push_back(script);
        other.Save(root / "Assets/Scenes/B.json");
        auto bad = other;
        bad.objects[0].SetModel("Assets/Models/missing.obj");
        bad.Save(root / "Assets/Scenes/Bad.json");
        SceneEnvironment environment;
        std::string error;
        Require(environment.Initialize(renderer, root, root / "Assets/Scenes/A.json", error),
                "initial collection prepares");
        environment.Update(1.0 / 60, true, true);
        bool deferred = false;
        Require(renderer.Render({0, 0, 0, 1},
                                [&](auto* commands, float)
                                {
                                    environment.Draw(commands, renderer.GetWidth(), renderer.GetHeight());
                                    environment.Click("parent");
                                    deferred = environment.World().Layout().objects.size() == 2;
                                }) != Engine::RenderResult::Failed &&
                    deferred,
                "UI scene click does not replace resources while recording GPU commands");
        environment.FlushSceneChanges();
        Require(environment.World().Layout().objects.size() == 4,
                "queued runtime additive scene prepares before the next draw");
        const auto snapshot = environment.World().Layout().Serialize();
        Require(!environment.LoadScene("Assets/Scenes/Bad.json", true, error) &&
                    environment.World().Layout().Serialize() == snapshot && environment.Scenes().Entries().size() == 2,
                "failed additive resources preserve world and ownership");
        Require(environment.UnloadScene("Assets/Scenes/A.json", error), "initial scene unloads");
        Require(environment.World().Layout().objects.size() == 4, "persistent root preserves its descendants");
        environment.Update(1.0 / 60, true, true);
        Require(environment.World().Layout().objects[0].position[2] == 2,
                "persistent script state survives additive unload");
        Require(environment.LoadScene("Assets/Scenes/B.json", false, error),
                "single scene switch keeps persistent subtree");
        environment.Update(1.0 / 60, true, true);
        Require(environment.World().Layout().objects[0].position[2] == 3,
                "persistent script state survives single switch");
        Require(
            environment.World().Layout().objects[2].position[2] == 1 && *stopped == 1,
            "same-ID single reload creates a new Script instance and stops the previous nonpersistent instance exactly once");
        Require(!environment.LoadScene("../outside.json", true, error), "scene path cannot escape content");
        Require(renderer.Render({0, 0, 0, 1},
                                [&](auto* commands, float)
                                {
                                    environment.Draw(commands, renderer.GetWidth(), renderer.GetHeight());
                                }) != Engine::RenderResult::Failed,
                "combined scene renders");
        Require(renderer.WaitForIdle(), "scene collection GPU completes");
        SceneEnvironment edited;
        auto editable = Scene();
        for (auto& object : editable.objects)
            object.scripts.clear();
        Require(edited.Initialize(renderer, root, editable, error) &&
                    edited.LoadScene("Assets/Scenes/B.json", true, error),
                "edited collection initializes");
        ScenePlacement leaf;
        leaf.id = "created-leaf";
        leaf.terrain.emplace();
        leaf.terrain->columns = 2;
        leaf.terrain->rows = 2;
        leaf.terrain->heights.assign(4, 0);
        std::string created;
        Require(edited.World().AddObject(leaf, root, created, error), "new leaf is created before its parent");
        ScenePlacement branch;
        branch.id = "created-branch";
        branch.parentId = "parent";
        Require(edited.World().AddObject(branch, root, created, error) &&
                    edited.World().SetParent(leaf.id, branch.id, error),
                "editor reparents earlier leaf under a new branch");
        Require(renderer.Render({0, 0, 0, 1},
                                [&](auto* commands, float)
                                {
                                    edited.Draw(commands, 640, 360);
                                }) != Engine::RenderResult::Failed &&
                    renderer.WaitForIdle(),
                "edited subtree GPU resources render before unloading");
        Require(edited.UnloadScene("Main", error) && edited.World().Layout().objects.size() == 2 &&
                    edited.World().Layout().objects.front().id == "parent-scene-1",
                "actual scene unload removes the full edited subtree");
        Require(renderer.Render({0, 0, 0, 1},
                                [&](auto* commands, float)
                                {
                                    edited.Draw(commands, 640, 360);
                                }) != Engine::RenderResult::Failed &&
                    renderer.WaitForIdle(),
                "remaining additive scene renders after edited subtree unload");
    }
}
