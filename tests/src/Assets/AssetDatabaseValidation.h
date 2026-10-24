#pragma once
#include <Engine/Assets/AssetDatabase.h>
#include <Engine/Graphics/Models/ModelLoader.h>
#include <Engine/Animation/Skeleton.h>
#include "../../../Editor/src/Assets/AssetChanges.h"
#include <SceneRuntime/Scene/SceneLayout.h>
#include <SceneRuntime/Rendering/MaterialAsset.h>
namespace AssetDatabaseValidation
{
    inline void Require(bool value, const char* message)
    {
        if (!value)
            throw std::runtime_error(message);
    }
    inline void AliasLimit()
    {
        using Engine::AssetDatabase;
        const auto root =
            std::filesystem::absolute("generated/tests/asset-alias-limit/" + std::to_string(GetCurrentProcessId()) +
                                      "-" + std::to_string(GetTickCount64()));
        const auto source = std::filesystem::path("Assets/Models/A.obj"),
                   target = std::filesystem::path("Assets/Models/Moved/B.obj");
        std::filesystem::create_directories((root / source).parent_path());
        {
            std::ofstream file(root / source);
            file << "# unchanged model\n";
        }
        auto metadata = AssetDatabase::Ensure(root / source);
        for (int i = 0; i < 1024; ++i)
            metadata.previousPaths.push_back("Assets/Models/Old" + std::to_string(i) + ".obj");
        AssetDatabase::Write(root / source, metadata);
        bool rejected = false;
        try
        {
            AssetDatabase(root).Move(source, target);
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        Require(rejected && std::filesystem::exists(root / source) &&
                    !std::filesystem::exists(root / target.parent_path()),
                "full alias history rejects move before filesystem changes");
        Require(AssetDatabase(root).Resolve(source, metadata.id) == source &&
                    AssetDatabase::Read(root / source).previousPaths == metadata.previousPaths,
                "rejected move preserves metadata and index");
        auto invalid = metadata;
        invalid.previousPaths.push_back("Assets/Models/Overflow.obj");
        rejected = false;
        try
        {
            AssetDatabase::Write(root / source, invalid);
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        Require(rejected && AssetDatabase::Read(root / source).previousPaths == metadata.previousPaths,
                "oversized alias write preserves sidecar");
        invalid = metadata;
        invalid.previousPaths[0] = "../Escape.obj";
        rejected = false;
        try
        {
            AssetDatabase::Write(root / source, invalid);
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        Require(rejected && AssetDatabase::Read(root / source).previousPaths == metadata.previousPaths,
                "invalid alias write preserves sidecar");
        metadata.previousPaths.back() = AssetDatabase::Text(source);
        AssetDatabase::Write(root / source, metadata);
        AssetDatabase(root).Move(source, target);
        Require(AssetDatabase::Read(root / target).previousPaths.size() == 1024 &&
                    AssetDatabase(root).Resolve(source, metadata.id) == target,
                "known alias at limit remains movable without duplicate history");
    }
    /// <summary>シェーダーの改名・移動後もincludeを解決し、変更や旧名の再利用を検出します。</summary>
    inline void ShaderMoves()
    {
        using Engine::AssetDatabase;
        const auto root = std::filesystem::absolute("generated/tests/shader-moves") /
                          (std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
        std::filesystem::create_directories(root / "Shaders/Nested");
        const auto write = [&](const char* path, const char* text)
        {
            std::ofstream file(root / path, std::ios::binary);
            file << text;
        };
        write("Shaders/Test.hlsl", "#include \"Shared.hlsli\"\nfloat4 PSMain() : SV_TARGET {return Value();}\n");
        write("Shaders/Shared.hlsli", "#include \"Nested/Color.hlsli\"\n");
        write("Shaders/Nested/Color.hlsli", "float4 Value() {return float4(1,0,0,1);}\n");
        Microsoft::WRL::ComPtr<ID3DBlob> original, moved, changed, recreated;
        const auto compile = [&](auto& output)
        {
            return Engine::CompileShader(root / "Shaders/Test.hlsl", "PSMain", "ps_5_0", output);
        };
        Require(compile(original), "shader include fixture compiles before moves");
        AssetDatabase(root).Move("Shaders/Shared.hlsli", "Shaders/Renamed.hlsli");
        Require(compile(moved), "shader include follows a renamed asset alias");
        AssetDatabase(root).Move("Shaders/Renamed.hlsli", "Shaders/Moved/Renamed.hlsli");
        Require(compile(moved), "moved include retains its relative nested dependencies");
        AssetDatabase(root).Move("Shaders/Nested/Color.hlsli", "Shaders/Else/Color.hlsli");
        Require(compile(moved), "nested include follows its own moved alias");
        write("Shaders/Else/Color.hlsli", "float4 Value() {return float4(0,1,0,1);}\n");
        Require(compile(changed) && changed.Get() != moved.Get(), "aliased include edits invalidate shader bytecode");
        write("Shaders/Shared.hlsli", "float4 Value() {return float4(0,0,1,1);}\n");
        Require(compile(recreated) && recreated.Get() != changed.Get(),
                "a recreated physical include overrides its old alias without stale cache");
        AssetDatabase(root).Move("Shaders/Test.hlsl", "Shaders/TestMoved.hlsl");
        Require(compile(moved), "main shader entry path follows a renamed asset alias");
        AssetDatabase(root).Move("Shaders/TestMoved.hlsl", "Shaders/Moved/TestMoved.hlsl");
        Require(compile(moved), "main shader folder move preserves its relative include and old entry path");
        write("Shaders/Macro.hlsl",
              "#define HEADER \"Shared.hlsli\"\n#include HEADER\nfloat4 PSMain() : SV_TARGET {return Value();}\n");
        const auto metadata = AssetDatabase::Ensure(root / "Shaders/Macro.hlsl");
        bool rejected = false;
        try
        {
            AssetDatabase(root).Move("Shaders/Macro.hlsl", "Shaders/Moved/Macro.hlsl");
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        Require(rejected && std::filesystem::is_regular_file(root / "Shaders/Macro.hlsl") &&
                    AssetDatabase::Read(root / "Shaders/Macro.hlsl").id == metadata.id &&
                    !std::filesystem::exists(root / "Shaders/Moved/Macro.hlsl"),
                "unrewritable macro include move preserves the shader and its ID");
    }
    inline void Run()
    {
        AliasLimit();
        ShaderMoves();
        using namespace Engine;
        using namespace SceneRuntime;
        const auto root =
            std::filesystem::absolute("generated/tests/asset-ids/" + std::to_string(GetCurrentProcessId()) + "-" +
                                      std::to_string(GetTickCount64()));
        std::filesystem::create_directories(root / "Assets/Models");
        std::filesystem::create_directories(root / "Assets/Scenes");
        const std::filesystem::path source = "Assets/Models/Original.obj",
                                    destination = "Assets/Models/Moved/Renamed.obj";
        {
            std::ofstream output(root / "Assets/Models/Original.mtl");
            output << "newmtl test\nKd 1 1 1\n";
        }
        {
            std::ofstream output(root / source);
            output << "mtllib Original.mtl\nusemtl test\nv 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0.25 0.25\nf 1/1 2/1 3/1\n";
        }
        auto metadata = AssetDatabase::Ensure(root / source);
        Require(AssetDatabase::Ensure(root / source).id == metadata.id, "stable metadata ID");
        SceneLayout scene;
        ScenePlacement actor;
        actor.id = "actor";
        actor.name = "Actor";
        actor.SetModel(source);
        scene.objects = {actor};
        scene.Save(root / "Assets/Scenes/Test.json");
        auto saved = SceneLayout::Load(root / "Assets/Scenes/Test.json", root);
        Require(saved.assetReferences.at(AssetDatabase::Text(source)) == metadata.id, "scene captures referenced ID");
        AssetDatabase(root).Move(source, destination);
        Require(AssetDatabase::Read(root / destination).id == metadata.id && !std::filesystem::exists(root / source),
                "rename preserves sidecar and ID");
        Require(AssetDatabase(root).Resolve(source) == destination &&
                    AssetDatabase(root).Resolve(source, metadata.id) == destination,
                "old paths and IDs resolve moved asset");
        saved = SceneLayout::Load(root / "Assets/Scenes/Test.json", root);
        Require(saved.objects[0].Model() == destination, "saved scene follows model rename");
        scene.ResolveAssets(root);
        Require(scene.objects[0].Model() == destination, "legacy in-memory layout follows rename");
        std::filesystem::create_directories(root / "Shaders");
        Editor::AssetChanges watcher;
        watcher.Observe(Editor::AssetChanges::Capture(root));
        metadata = AssetDatabase::Read(root / destination);
        metadata.scale = 2;
        metadata.flipV = true;
        AssetDatabase::Write(root / destination, metadata);
        watcher.Observe(Editor::AssetChanges::Capture(root));
        Require(watcher.Pending(), "saved import metadata requests reload");
        watcher.Observe(Editor::AssetChanges::Capture(root));
        Require(watcher.TakeReady(true), "stable import metadata reloads once");
        const auto skeletal = root / "Assets/Models/Skeleton.gltf";
        std::filesystem::copy_file("Games/GuruttoWorld/Assets/Models/AnimatedBox.gltf", skeletal);
        auto skeletalSettings = AssetDatabase::Ensure(skeletal);
        skeletalSettings.scale = 2;
        AssetDatabase::Write(skeletal, skeletalSettings);
        std::string error;
        const auto rig = Skeleton::Load(skeletal, error);
        Require(rig != nullptr, "skeletal import settings load");
        const auto skinned =
            Skeleton::Skin(*rig, rig->meshes[0], Skeleton::Matrices(*rig, Skeleton::Sample(*rig, "", 0, false)));
        float height = 0;
        for (const auto& vertex : skinned.vertices)
            height = std::max(height, vertex.position[1]);
        Require(std::abs(height - 4) < 0.001f, "skeletal import scale applies after skinning");
        std::vector<MeshData> meshes;
        Require(ModelLoader::Load(root / destination, meshes), "model reimports with metadata");
        float maximum = 0;
        for (const auto& vertex : meshes[0].vertices)
            maximum = std::max(maximum, vertex.position[0]);
        Require(maximum == 2 && std::abs(meshes[0].vertices[0].uv[1] - 0.25f) < 0.001f,
                "import scale and UV flip apply");
        bool rejected = false;
        try
        {
            AssetDatabase(root).Move(destination, "Assets/Models/../../Escape.obj");
        }
        catch (...)
        {
            rejected = true;
        }
        Require(rejected && std::filesystem::exists(root / destination), "traversal refused without moving source");
        {
            std::ofstream output(root / "Assets/Models/Exists.obj");
            output << "existing";
        }
        rejected = false;
        try
        {
            AssetDatabase(root).Move(destination, "Assets/Models/Exists.obj");
        }
        catch (...)
        {
            rejected = true;
        }
        Require(rejected, "rename cannot overwrite existing asset");
        metadata.scale = 0;
        rejected = false;
        try
        {
            AssetDatabase::Write(root / destination, metadata);
        }
        catch (...)
        {
            rejected = true;
        }
        Require(rejected && AssetDatabase::Read(root / destination).scale == 2, "invalid settings preserve metadata");
        std::filesystem::remove(AssetDatabase::Sidecar(root / destination));
        rejected = false;
        try
        {
            SceneLayout::Load(root / "Assets/Scenes/Test.json", root);
        }
        catch (...)
        {
            rejected = true;
        }
        Require(rejected, "missing stable ID reported instead of substituting asset");
    }
}
