#pragma once
#include <SceneRuntime/SceneMigration.h>
#include "../Editor/src/SceneSnapshotCache.h"
#include "../Editor/src/ProjectCatalog.h"
#include <SceneRuntime/SceneCollection.h>
#include <SceneRuntime/SceneWorld.h>
#include <SceneRuntime/Prefab.h>
#include <Engine/Graphics/Models/ModelLoader.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <fstream>

namespace FollowupStateValidation
{
    inline void Require(bool value, const char* message)
    {
        if (!value)
            throw std::runtime_error(message);
    }
    inline std::filesystem::path Root(const char* name)
    {
        return std::filesystem::absolute(std::string("generated/tests/followup-state/") + name + "/" +
                                         std::to_string(GetCurrentProcessId()) + "-" +
                                         std::to_string(GetTickCount64()));
    }
    inline void File(const std::filesystem::path& path, const std::string& contents = "{}")
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary);
        output << contents;
    }
    inline void ReusedPaths()
    {
        using namespace SceneRuntime;
        using namespace Engine;
        const auto root = Root("paths");
        const std::string a = "Assets/Models/A.obj", b = "Assets/Models/B.obj", c = "Assets/Models/C.obj";
        File(root / a);
        const auto original = AssetDatabase::Ensure(root / a);
        SceneLayout scene;
        ScenePlacement actor;
        actor.id = "actor";
        actor.SetModel(a);
        scene.objects = {actor};
        std::filesystem::create_directories(root / "Assets/Scenes");
        scene.Save(root / "Assets/Scenes/Test.json");
        scene = SceneLayout::Load(root / "Assets/Scenes/Test.json", root);
        AssetDatabase(root).Move(a, b);
        scene.ResolveAssets(root);
        Require(scene.objects[0].Model() == b && !scene.assetReferences.contains(a),
                "Resolved references discard obsolete path IDs");
        File(root / a);
        const auto replacement = AssetDatabase::Ensure(root / a);
        scene.objects[0].SetModel(a);
        scene.ResolveAssets(root);
        Require(scene.objects[0].Model() == a && scene.assetReferences.at(a) == replacement.id,
                "Explicit reused-path selection keeps the newly selected asset");
        AssetDatabase(root).Move(a, c);
        AssetDatabase index(root);
        Require(index.Resolve(a, original.id) == b && index.Resolve(a, replacement.id) == c,
                "Ambiguous aliases do not prevent stable ID resolution");
        Require(SceneLayout::Load(root / "Assets/Scenes/Test.json", root).objects[0].Model() == b,
                "Saved original references do not switch to a reused path");
        bool rejected = false;
        try
        {
            static_cast<void>(index.Resolve(a));
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        Require(rejected, "An ambiguous legacy path fails locally instead of selecting an arbitrary asset");
        Editor::ProjectCatalog catalog;
        Require(catalog.Scan(root), "Project catalog remains usable with reused aliases");
        scene.ResolveAssets(root);
        scene.Save(root / "Assets/Scenes/Selected.json");
        Require(SceneLayout::Load(root / "Assets/Scenes/Selected.json", root).objects[0].Model() == c,
                "New selection retains its stable ID after another move and save");
        auto unresolved = SceneLayout::Load(root / "Assets/Scenes/Test.json");
        unresolved.Save(root / "Assets/Scenes/Unresolved.json");
        Require(SceneLayout::Load(root / "Assets/Scenes/Unresolved.json", root).objects[0].Model() == b,
                "Saving an unresolved old path preserves its stable ID");
    }
    inline void DefaultReferences()
    {
        using namespace SceneRuntime;
        using namespace Engine;
        const auto root = Root("defaults");
        std::filesystem::create_directories(root / "Assets/Scenes");
        const std::string old = "Assets/Scenes/Old.json", moved = "Assets/Scenes/New.json";
        SceneLayout{}.Save(root / old);
        AssetDatabase::Ensure(root / old);
        ScriptDefinition definition;
        definition.dataFields = {{"destination", {old}}, {"label", {std::string("Plain default")}}};
        definition.dataPurposes = {{"destination", ScriptDataPurpose::SceneAsset}};
        definition.update = [](ScriptContext& context)
        {
            context.scene->LoadScene(std::get<std::string>(context.Data("destination")->value));
        };
        std::string error;
        Require(ScriptRegistry::InstallModule("followup-defaults", {{"DefaultReferenceRegression", definition}}, error),
                "Default reference definition installs");
        SceneLayout scene;
        ScenePlacement actor;
        actor.id = "actor";
        actor.scripts.emplace_back();
        actor.scripts[0].behaviour = "DefaultReferenceRegression";
        scene.objects = {actor};
        Require(MigrateSceneScripts(scene, {{"DefaultReferenceRegression", definition}}).conflicts.empty(),
                "Defaults migrate explicitly before saving");
        const auto migrated = scene.Serialize();
        scene.Save(root / "Assets/Scenes/Scripted.json");
        Require(scene.Serialize() == migrated, "Saving does not change migrated data");
        AssetDatabase(root).Move(old, moved);
        auto loaded = SceneLayout::Load(root / "Assets/Scenes/Scripted.json", root);
        Require(loaded.objects[0].scripts[0].data.contains("label"), "Explicit migration materializes all defaults");
        ScriptRuntime runtime;
        Require(runtime.UpdateCandidate(loaded, .016, error), "Default reference script runs");
        const auto commands = runtime.TakeSceneCommands();
        Require(commands.size() == 1 && commands[0].scene == moved,
                "Unedited default scene reference follows an asset rename at runtime");
        static_cast<void>(SceneLayout::Load(root / commands[0].scene, root));
        Require(ScriptRegistry::InstallModule("followup-defaults", {}, error), "Default definition can be unloaded");
        loaded = SceneLayout::Load(root / "Assets/Scenes/Scripted.json", root);
        Require(loaded.objects[0].scripts[0].sceneAssets.contains("destination") &&
                    std::get<std::string>(loaded.objects[0].scripts[0].data.at("destination").value) == moved,
                "Saved default references remain usable without their module");
    }
    inline void SchemaSnapshots()
    {
        using namespace SceneRuntime;
        ScriptDefinition definition;
        definition.update = [](ScriptContext&) {};
        definition.dataFields = {{"destination", {std::string("Assets/Scenes/Old.json")}}};
        std::string error;
        Require(ScriptRegistry::InstallModule("followup-cache", {{"SnapshotSchemaRegression", definition}}, error),
                "Snapshot definition installs");
        SceneLayout scene;
        ScenePlacement actor;
        actor.id = "actor";
        actor.scripts.emplace_back();
        actor.scripts[0].behaviour = "SnapshotSchemaRegression";
        scene.objects = {actor};
        Editor::SceneSnapshotCache cache;
        const auto before = cache.Get(scene);
        definition.dataPurposes = {{"destination", ScriptDataPurpose::SceneAsset}};
        Require(ScriptRegistry::InstallModule("followup-cache", {{"SnapshotSchemaRegression", definition}}, error),
                "Changed reference purpose installs");
        Require(cache.Get(scene) == before && cache.Serializations() == 1,
                "Module purpose changes preserve Undo and recovery snapshots");
        const auto revision = ScriptRegistry::Revision(), count = cache.Serializations();
        auto invalid = definition;
        invalid.fields["invalid"] = {2, 0, 1};
        Require(!ScriptRegistry::InstallModule("followup-cache", {{"SnapshotSchemaRegression", invalid}}, error) &&
                    ScriptRegistry::Revision() == revision,
                "Rejected definitions do not advance the registry revision");
        cache.Get(scene);
        Require(cache.Serializations() == count, "Stable definitions reuse the snapshot cache");
        definition.dataFields["destination"] = {std::string("Assets/Scenes/New.json")};
        Require(ScriptRegistry::InstallModule("followup-cache", {{"SnapshotSchemaRegression", definition}}, error),
                "Changed reference default installs");
        Require(cache.Get(scene) == before && cache.Serializations() == count,
                "Changed defaults leave document snapshots unchanged");
        Require(MigrateSceneScripts(scene, {{"SnapshotSchemaRegression", definition}}).conflicts.empty(),
                "Snapshot schema migrates explicitly");
        const auto migrated = cache.Get(scene);
        Require(migrated != before && cache.Serializations() == count + 1,
                "Explicit migration invalidates the document snapshot");
        Require(ScriptRegistry::InstallModule("followup-cache", {}, error), "Snapshot module can be removed");
        Require(cache.Get(scene) == migrated && cache.Serializations() == count + 1,
                "Module removal preserves migrated snapshots");
    }
    inline void PersistentPrefabs()
    {
        using namespace SceneRuntime;
        const auto root = Root("prefabs");
        std::filesystem::create_directories(root / "Assets/Prefabs");
        const std::string path = "Assets/Prefabs/Persistent.prefab";
        SceneLayout asset;
        ScenePlacement actor;
        actor.id = "root";
        actor.persistent = true;
        asset.objects = {actor};
        asset.Save(root / path);
        SceneLayout scene;
        const auto id = Prefab::Instantiate(scene, asset, path, {0, 0, 0});
        Prefab::Refresh(scene, root);
        Require(scene.objects[0].persistent, "Prefab refresh preserves source persistence");
        scene.objects[0].persistent = false;
        Prefab::Refresh(scene, root);
        const auto overrides = Prefab::Overrides(scene, id);
        Require(!scene.objects[0].persistent && overrides.size() == 1 && overrides[0].path == "/persistent",
                "Local persistence override survives refresh and appears in Inspector overrides");
        Prefab::RevertOverride(scene, id, "/persistent");
        Require(scene.objects[0].persistent, "Persistence override can be reverted individually");
        scene.objects[0].persistent = false;
        Prefab::ApplyOverride(scene, id, "/persistent", root);
        Prefab::Refresh(scene, root);
        Require(!SceneLayout::Load(root / path).objects[0].persistent && Prefab::Overrides(scene, id).empty(),
                "Persistence override can be saved to the source");
        asset = SceneLayout::Load(root / path);
        asset.objects[0].persistent = true;
        asset.Save(root / path);
        Prefab::Refresh(scene, root);
        Require(scene.objects[0].persistent, "Source persistence updates propagate to unmodified instances");
        SceneCollection collection;
        collection.Reset(scene, "Owner");
        Require(collection.Unload(scene, "Owner").objects.size() == 1,
                "Refreshed persistent prefab survives its scene unload");
        scene.objects[0].persistent = false;
        Prefab::RevertInstance(scene, id, root);
        Require(scene.objects[0].persistent, "Whole-instance revert restores persistence");
    }
    inline void ModelMoves()
    {
        using namespace Engine;
        const auto root = Root("models");
        std::ifstream input("Games/GuruttoWorld/Assets/Models/AnimatedBox.gltf");
        auto document = Json::parse(input);
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
        File(root / "Assets/Models/buffer.bin", bytes);
        document["buffers"][0]["uri"] = "buffer.bin";
        File(root / "Assets/Models/Collider.GLTF", document.dump());
        std::vector<MeshData> meshes;
        Require(ModelLoader::Load(root / "Assets/Models/Collider.GLTF", meshes), "Uppercase glTF fixture loads");
        AssetDatabase(root).Move("Assets/Models/Collider.GLTF", "Assets/Models/Sub/Collider.GLTF");
        meshes.clear();
        Require(ModelLoader::Load(root / "Assets/Models/Sub/Collider.GLTF", meshes),
                "Uppercase glTF move rewrites external dependencies");
        File(root / "Assets/Models/Material.mtl", "newmtl test\nKd 1 1 1\n");
        File(root / "Assets/Models/Triangle.OBJ",
             "mtllib Material.mtl\nusemtl test\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
        AssetDatabase(root).Move("Assets/Models/Triangle.OBJ", "Assets/Models/Sub/Triangle.OBJ");
        std::ifstream obj(root / "Assets/Models/Sub/Triangle.OBJ");
        std::string line;
        std::getline(obj, line);
        Require(line == "mtllib ../Material.mtl", "Uppercase OBJ move rewrites its material dependency");
    }
    inline void EditorOwnership(Engine::DirectX12Renderer& renderer)
    {
        using namespace SceneRuntime;
        const auto root = std::filesystem::absolute("Games/GuruttoWorld");
        std::string error;
        SceneLayout main;
        ScenePlacement first;
        first.id = "first";
        ScenePlacement removed;
        removed.id = "removed";
        main.objects = {first, removed};
        SceneLayout other;
        ScenePlacement foreign;
        foreign.id = "foreign";
        other.objects = {foreign};
        SceneCollection collection;
        collection.Reset(main, "Main");
        const auto merged = collection.Add(main, other, "Other");
        SceneWorld world;
        Require(world.Initialize(renderer, root, merged, root / "Shaders/Mesh.hlsl", &error),
                "Editor ownership world initializes");
        Require(world.RemoveObjects({"removed"}, error), "Editor deletion succeeds");
        const auto saved = SceneLayout::Parse(world.Layout().Serialize());
        Require(saved.sceneObjects.size() == 2 && saved.sceneObjects.at("Main") == std::vector<std::string>{"first"},
                "Deletion preserves surviving ownership through save and reload");
        collection.Reset(saved, "Main");
        const auto afterUnload = collection.Unload(saved, "Other");
        Require(afterUnload.objects.size() == 1 && afterUnload.objects[0].id == "first",
                "Other still unloads independently after deletion");
        Require(world.ReplaceLayout(merged, root, error), "Duplicate fixture resets");
        std::string copied;
        Require(world.DuplicateObject("first", {0, 0, 0}, copied, error), "Editor duplication succeeds");
        const auto duplicated = SceneLayout::Parse(world.Layout().Serialize());
        collection.Reset(duplicated, "Main");
        const auto withoutOther = collection.Unload(duplicated, "Other");
        Require(std::any_of(withoutOther.objects.begin(), withoutOther.objects.end(),
                            [&](const auto& p)
                            {
                                return p.id == copied;
                            }),
                "Main duplicate survives Other unload after save and reload");
        collection.Reset(duplicated, "Main");
        const auto withoutMain = collection.Unload(duplicated, "Main");
        Require(withoutMain.objects.size() == 1 && withoutMain.objects[0].id == "foreign",
                "Duplicate unloads with its original owner");
    }
    inline void ProceduralEdits(Engine::DirectX12Renderer& renderer)
    {
        using namespace SceneRuntime;
        const auto root = std::filesystem::absolute("Games/GuruttoWorld");
        std::string error;
        SceneLayout layout;
        ScenePlacement ground;
        ground.id = "ground";
        ground.terrain.emplace();
        ground.terrain->columns = 2;
        ground.terrain->rows = 2;
        ground.terrain->heights.assign(4, 0);
        layout.objects = {ground};
        SceneWorld world;
        Require(world.Initialize(renderer, root, layout, root / "Shaders/Mesh.hlsl", &error),
                "Terrain edit fixture initializes");
        ground.terrain->heights.assign(4, 2);
        Require(world.SetComponents("ground", ground, root, error), "Terrain editor change succeeds");
        Require(world.PickRay({.5f, 3, .5f}, {0, -1, 0}, 2) == "ground",
                "Terrain picking reflects edited heights immediately");
        auto invalid = ground;
        invalid.terrain->heights.pop_back();
        const auto snapshot = world.Layout().Serialize();
        Require(!world.SetComponents("ground", invalid, root, error) && world.Layout().Serialize() == snapshot &&
                    world.PickRay({.5f, 3, .5f}, {0, -1, 0}, 2) == "ground",
                "Invalid terrain edit preserves data and rendered geometry");
        ScenePlacement tiles;
        tiles.id = "tiles";
        tiles.tilemap.emplace();
        tiles.tilemap->columns = 2;
        tiles.tilemap->rows = 2;
        tiles.tilemap->tiles = {0, -1, -1, -1};
        layout.objects = {tiles};
        Require(world.ReplaceLayout(layout, root, error), "Tile edit fixture initializes");
        const std::array<float, 3> ray{1.5f, .5f, -2};
        Require(!world.PickRay(ray, {0, 0, 1}, 4), "Empty tile has no pick geometry");
        tiles.tilemap->tiles[1] = 0;
        Require(world.SetComponentBatch({tiles}, root, error) && world.PickRay(ray, {0, 0, 1}, 4) == "tiles",
                "Batch tile edit updates geometry immediately");
        Require(world.ReplaceLayout(SceneLayout::Parse(snapshot), root, error) &&
                    world.PickRay({.5f, 3, .5f}, {0, -1, 0}, 2) == "ground",
                "Saved terrain geometry reloads identically");
    }
    inline void Run()
    {
        ReusedPaths();
        DefaultReferences();
        SchemaSnapshots();
        PersistentPrefabs();
        ModelMoves();
    }
}
