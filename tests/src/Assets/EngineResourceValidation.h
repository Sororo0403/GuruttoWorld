#pragma once
#include <Engine/Assets/AssetDatabase.h>
#include <Engine/Graphics/ShaderCompiler.h>
#include <SceneRuntime/Rendering/MaterialAsset.h>
#include "../../../Editor/src/Project/ProjectCatalog.h"
#include "../../../Editor/src/Assets/AssetChanges.h"

namespace EngineResourceValidation
{
    /// <summary>標準リソースとゲーム素材の所有境界、旧参照、ID、監視を検証します。</summary>
    inline void Run(const std::filesystem::path& fixture)
    {
        using Engine::AssetDatabase;
        using Engine::ContentFiles;
        const auto require = [](bool value, const char* message)
        {
            if (!value)
                throw std::runtime_error(message);
        };
        const auto write = [](const std::filesystem::path& path, const std::string& source)
        {
            std::filesystem::create_directories(path.parent_path());
            Engine::FileContents::Write(path, source);
        };
        const auto game = fixture / "Game";
        const auto engine = fixture / "SDK";
        const auto shader = engine / "Engine/Shaders/Mesh.hlsl";
        const auto logical = std::filesystem::path("Engine/Shaders/Mesh.hlsl");
        const std::string id(32, '1');
        const std::string source =
            "#include \"Common/Value.hlsli\"\nfloat4 VSMain(float4 p:POSITION):SV_POSITION{return p;}\nfloat4 PSMain():SV_TARGET{return Value();}\n";
        write(game / "WP1.project.json", Engine::Json{{"engineRoot", "../SDK"}}.dump());
        write(game / "Assets/Project.json", "{}");
        write(shader, source);
        write(AssetDatabase::Sidecar(shader), Engine::Json{{"id", id}}.dump());
        write(engine / "Engine/Shaders/Common/Value.hlsli", "float4 Value(){return float4(1,0,0,1);}");
        write(engine / "Editor/Resources/Fonts/Test.ttf", "editor only");
        const auto context = Engine::ContentContext::Capture(game);
        require(context->Resolve(logical) == shader, "Explicit engine resource resolves to its owner");
        require(AssetDatabase(game).Resolve("Shaders/Mesh.hlsl", id) == logical,
                "Legacy shader IDs resolve to the engine namespace");
        require(AssetDatabase(game).Resolve("Shaders/Mesh.hlsl") == logical,
                "Legacy paths remain readable without copying standard resources");
        Editor::ProjectCatalog catalog;
        require(catalog.Scan(game) && catalog.Assets().empty(),
                "Game catalog excludes engine shaders and editor fonts");
        write(game / "Shaders/Mesh.hlsl", "game shader");
        const auto local = AssetDatabase::Ensure(game / "Shaders/Mesh.hlsl");
        require(local.id != id && context->Resolve(logical) == shader &&
                    context->Resolve("Shaders/Mesh.hlsl") == game / "Shaders/Mesh.hlsl",
                "Game shader does not replace the engine renderer resource");
        require(catalog.Scan(game) && catalog.Assets().size() == 1, "Game shader appears once as an owned asset");
        bool rejected = false;
        try
        {
            AssetDatabase::Write(game / logical, AssetDatabase::Read(game / logical));
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        require(rejected && !std::filesystem::exists(game / logical), "Game cannot write engine metadata");
        SceneRuntime::MaterialAsset material;
        material.shader = logical;
        material.Save(game, "Assets/Materials/Default.mat");
        require(SceneRuntime::MaterialAsset::Load(game, "Assets/Materials/Default.mat").shader == logical,
                "Material preserves explicit resource references");
        std::string before, after;
        const auto observed = Editor::AssetChanges::Capture(game);
        require(Engine::GetShaderRevision(game / logical, before), "Standard shader preprocesses with owned includes");
        write(engine / "Engine/Shaders/Common/Value.hlsli", "float4 Value(){return float4(0,1,0,1);}");
        require(observed != Editor::AssetChanges::Capture(game), "Editor detects standard shader include edits");
        const auto edited = Editor::AssetChanges::Capture(game);
        write(engine / "Engine/Shaders/Compiled/Ignore.bin", "bytecode");
        require(edited == Editor::AssetChanges::Capture(game), "Bytecode caches do not trigger asset reloads");
        require(Engine::GetShaderRevision(game / logical, after) && before != after,
                "Engine include edits invalidate shader revision");
        AssetDatabase(game).Move("Shaders/Mesh.hlsl", "Shaders/Renamed.hlsl");
        require(AssetDatabase(game).Resolve("Shaders/Mesh.hlsl", local.id) == "Shaders/Renamed.hlsl" &&
                    AssetDatabase(game).Resolve("Shaders/Mesh.hlsl") == "Shaders/Renamed.hlsl",
                "Game assets keep ordinary rename behavior");
        const auto namedEngine = fixture / "Engine";
        write(namedEngine / "WP1.project.json", Engine::Json{{"engineRoot", "../SDK"}}.dump());
        write(namedEngine / "Shaders/Custom.hlsl", "game shader");
        require(ContentFiles::Root(namedEngine / "Shaders/Custom.hlsl") == namedEngine &&
                    ContentFiles::Resolve(namedEngine / "Shaders/Custom.hlsl") == namedEngine / "Shaders/Custom.hlsl",
                "A game folder named Engine retains its project boundary");
        const auto late = fixture / "LateGame";
        write(late / "WP1.project.json", Engine::Json{{"engineRoot", "../LateSDK"}}.dump());
        write(late / "Assets/Project.json", "{}");
        const auto lateContext = Engine::ContentContext::Capture(late);
        require(AssetDatabase(lateContext, true).ReferenceId(logical).empty(), "Missing resources are not indexed");
        write(fixture / "LateSDK/Engine/Shaders/Mesh.hlsl", source);
        write(fixture / "LateSDK/Engine/Shaders/Mesh.hlsl.meta", Engine::Json{{"id", id}}.dump());
        require(AssetDatabase(lateContext, true).ReferenceId(logical) == id,
                "Installing missing engine resources refreshes the index");
    }
}
