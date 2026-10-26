#pragma once
#include "../Support/TestGameContent.h"
#include <SceneRuntime/Scene/SceneMigration.h>
#include "../../../Editor/src/Documents/SceneSnapshotCache.h"
#include <SceneRuntime/Scene/SceneCollection.h>
#include <SceneRuntime/Scripts/ScriptRuntime.h>
#include <SceneRuntime/Physics/PhysicsWorld.h>
#include <Engine/Assets/AssetDatabase.h>
#include <Engine/Core/Profiler.h>
#include <fstream>

namespace DeepStateValidation
{
    inline void Require(bool value, const char* message)
    {
        if (!value)
            throw std::runtime_error(message);
    }
    inline void Ownership()
    {
        using namespace SceneRuntime;
        SceneLayout scene;
        ScenePlacement actor;
        actor.id = "actor";
        scene.objects = {actor};
        Editor::SceneSnapshotCache cache;
        const auto before = cache.Get(scene);
        scene.sceneObjects = {{"Main", {"actor"}}};
        Require(cache.Get(scene) != before && cache.Get(scene) == scene.Serialize(),
                "Ownership-only changes must invalidate snapshots");
        SceneCollection collection;
        collection.Reset(scene, "Main");
        scene.objects.clear();
        scene.sceneObjects.clear();
        SceneLayout incoming;
        incoming.objects = {actor};
        const auto added = collection.Add(scene, incoming, "Other");
        Require(added.sceneObjects.at("Main").empty() && added.sceneObjects.at("Other").size() == 1,
                "Deleted IDs must not retain old owners");
        Require(collection.Unload(added, "Main").objects.size() == 1,
                "Unloading old owner must preserve reused IDs in another scene");
        auto malformed = added;
        malformed.sceneObjects["Main"] = {"actor"};
        bool rejected = false;
        try
        {
            malformed.Validate();
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        Require(rejected, "Duplicate ownership must be rejected");
    }
    inline void References()
    {
        using namespace SceneRuntime;
        using namespace Engine;
        const auto root =
            std::filesystem::absolute("generated/tests/typed-references/" + std::to_string(GetCurrentProcessId()) +
                                      "-" + std::to_string(GetTickCount64()));
        std::filesystem::create_directories(root / "Assets/Scenes");
        const std::string original = "Assets/Scenes/Old.json", moved = "Assets/Scenes/New.json";
        {
            std::ofstream output(root / original);
            output << "{}";
        }
        AssetDatabase::Ensure(root / original);
        ScriptDefinition definition;
        definition.update = [](ScriptContext&) {};
        definition.dataFields = {{"destination", {original}}, {"description", {original}}, {"texture", {original}}};
        definition.dataPurposes = {{"destination", ScriptDataPurpose::SceneAsset}};
        Require(ScriptRegistry::Register("TypedReferenceAudit", definition), "Reference fixture registers");
        SceneLayout scene;
        ScenePlacement actor;
        actor.id = "actor";
        actor.name = original;
        actor.text.emplace();
        actor.text->text = original;
        actor.button.emplace();
        actor.button->action = "loadScene";
        actor.button->target = original;
        actor.scripts = {{"rules", true, "TypedReferenceAudit", {}, definition.dataFields}};
        scene.objects = {actor};
        Require(MigrateSceneScripts(scene, {{"TypedReferenceAudit", definition}}).conflicts.empty(),
                "Reference purposes migrate explicitly before saving");
        scene.Save(root / "Assets/Scenes/Test.json");
        AssetDatabase(root).Move(original, moved);
        const auto loaded = SceneLayout::Load(root / "Assets/Scenes/Test.json", root);
        const auto& saved = loaded.objects[0];
        Require(saved.name == original && saved.text->text == original,
                "Ordinary object names and Text must not be rewritten");
        Require(saved.button->target == moved, "Declared Button scene target must follow rename");
        Require(std::get<std::string>(saved.scripts[0].data.at("destination").value) == moved,
                "Declared Script scene assets must follow rename");
        Require(std::get<std::string>(saved.scripts[0].data.at("description").value) == original &&
                    std::get<std::string>(saved.scripts[0].data.at("texture").value) == original,
                "Plain Script data must preserve path-looking text");
        Require(saved.scripts[0].sceneAssets.contains("destination"),
                "Script reference purpose must survive saving and parsing");
    }
    inline void RootQueries()
    {
        using namespace SceneRuntime;
        SceneLayout before;
        ScenePlacement actor;
        actor.id = "actor";
        actor.boxCollider.emplace();
        ScenePlacement wall = actor;
        wall.id = "wall";
        wall.position[0] = 2;
        before.objects = {actor, wall};
        PhysicsWorld cached;
        std::string error;
        const auto constrain = [&](PhysicsWorld& world, const SceneLayout& layout)
        {
            auto candidate = layout;
            candidate.objects[0].position[0] = 4;
            Require(world.ConstrainRootMotion(layout, candidate, {0}, {}, error), "Root query succeeds");
            return candidate;
        };
        const auto blocked = constrain(cached, before);
        Require(blocked.objects[0].position[0] < 2, "Root movement collides with wall");
        Engine::CpuProfiler::Take();
        Require(constrain(cached, before).Serialize() == blocked.Serialize(),
                "Reused query restores poses before repeating a frame");
        const auto samples = Engine::CpuProfiler::Take();
        Require(std::none_of(samples.begin(), samples.end(),
                             [](const auto& sample)
                             {
                                 return sample.name == "Root collision world rebuild";
                             }),
                "Stable root queries must not rebuild the physics world");
        before.objects[1].position[0] = 6;
        Require(constrain(cached, before).objects[0].position[0] > 3.9f, "Cached obstacles follow changed transforms");
        before.objects[1].position[0] = 2;
        before.objects[0].scale = {2, 2, 2};
        PhysicsWorld fresh;
        Require(constrain(cached, before).Serialize() == constrain(fresh, before).Serialize(),
                "Changed shape scale matches a fresh collision world");
        before.objects[1].boxCollider->enabled = false;
        before.objects[1].boxCollider->shape = "mesh";
        Require(constrain(cached, before).objects[0].position[0] > 3.9f,
                "Collider configuration changes invalidate query world");
        ScenePhysics::States states;
        Require(cached.Advance(before, states, .01, 0, 0, false, {}, error),
                "Physical world initializes independently");
        const auto hit = cached.Raycast({0, 3, 0}, {0, -1, 0}, 10);
        const auto events = cached.Events();
        constrain(cached, before);
        const auto afterHit = cached.Raycast({0, 3, 0}, {0, -1, 0}, 10);
        Require(hit && afterHit && hit->object == afterHit->object && hit->distance == afterHit->distance &&
                    cached.Events().size() == events.size(),
                "Root queries preserve physical poses and contacts");
    }
    inline void GeometryFiles()
    {
        using namespace SceneRuntime;
        std::ifstream input(WP1_TEST_GAME_ROOT "/Assets/Models/AnimatedBox.gltf");
        auto document = Engine::Json::parse(input);
        const auto uri = document.at("buffers").at(0).at("uri").get<std::string>();
        const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string bytes;
        unsigned int accumulator = 0;
        int bits = -8;
        for (const auto character : uri.substr(uri.find(',') + 1))
        {
            const auto index = alphabet.find(character);
            if (index == std::string::npos)
                break;
            accumulator = (accumulator << 6) | static_cast<unsigned int>(index);
            bits += 6;
            if (bits >= 0)
            {
                bytes += static_cast<char>(accumulator >> bits);
                bits -= 8;
            }
        }
        const auto root =
            std::filesystem::absolute("generated/tests/root-query-files/" + std::to_string(GetCurrentProcessId()) +
                                      "-" + std::to_string(GetTickCount64()));
        std::filesystem::create_directories(root / "Assets/Models");
        document["buffers"][0]["uri"] = "buffer%20data.bin";
        {
            std::ofstream output(root / "Assets/Models/Collider.GLTF");
            output << document.dump();
        }
        {
            std::ofstream output(root / "Assets/Models/buffer%20data.bin", std::ios::binary);
            output << bytes;
        }
        SceneLayout before;
        ScenePlacement actor;
        actor.id = "actor";
        actor.boxCollider.emplace();
        ScenePlacement wall = actor;
        wall.id = "wall";
        wall.position[0] = 6;
        wall.boxCollider->shape = "mesh";
        wall.boxCollider->model = "Assets/Models/Collider.GLTF";
        before.objects = {actor, wall};
        PhysicsWorld physics;
        std::string error;
        auto candidate = before;
        candidate.objects[0].position[0] = 4;
        Require(physics.ConstrainRootMotion(before, candidate, {0}, root, error),
                "External encoded geometry buffer loads");
        candidate = before;
        candidate.objects[0].position[0] = 4;
        Require(physics.ConstrainRootMotion(before, candidate, {0}, root, error),
                "External geometry query cache reuses valid shapes");
        const auto buffer = root / "Assets/Models/buffer%20data.bin";
        const auto modified = std::filesystem::last_write_time(buffer);
        Engine::FileContents::Write(buffer, std::string(bytes.size(), static_cast<char>(255)));
        std::filesystem::last_write_time(buffer, modified);
        const auto unchanged = candidate.Serialize();
        Require(!physics.ConstrainRootMotion(before, candidate, {0}, root, error) && candidate.Serialize() == unchanged,
                "same-size geometry edits with restored timestamps invalidate collision queries");
        Engine::FileContents::Write(buffer, bytes);
        std::filesystem::last_write_time(buffer, modified);
        Require(physics.ConstrainRootMotion(before, candidate, {0}, root, error),
                "restored external geometry recovers collision queries");
        {
            std::ofstream output(root / "Assets/Models/buffer%20data.bin", std::ios::binary | std::ios::trunc);
        }
        const auto snapshot = candidate.Serialize();
        Require(!physics.ConstrainRootMotion(before, candidate, {0}, root, error) && candidate.Serialize() == snapshot,
                "Changed external geometry invalidates cache and preserves failed candidate");
    }
    inline void Run()
    {
        Ownership();
        References();
        RootQueries();
        GeometryFiles();
    }
}
