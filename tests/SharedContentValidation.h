#pragma once
#include <Engine/Assets/AssetDatabase.h>
#include <Engine/Graphics/ShaderCompiler.h>
#include <SceneRuntime/MaterialAsset.h>
#include "../Editor/src/ProjectCatalog.h"
#include "../Editor/src/AssetChanges.h"
#include "../Editor/src/AssetInfo.h"

namespace SharedContentValidation
{
    /// <summary>共有素材の参照、ID、上書き、変更監視を隔離したプロジェクトで検証します。</summary>
    inline void Run(const std::filesystem::path& fixture)
    {
        using Engine::AssetDatabase;
        using Engine::ContentFiles;
        const auto require = [](bool result, const char* message)
        {
            if (!result)
                throw std::runtime_error(message);
        };
        require(!std::filesystem::exists(fixture), "Choose a new shared-content fixture directory");
        const auto game = fixture / "Game";
        const auto common = fixture / "Engine/Content";
        const auto write = [](const std::filesystem::path& path, const std::string& text)
        {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream output(path, std::ios::binary);
            output << text;
            output.close();
            if (!output)
                throw std::runtime_error("Cannot write shared-content fixture");
        };
        const std::string id(32, '1');
        const std::string main = "#include \"Common/Value.hlsli\"\n"
                                 "float4 VSMain(float4 p:POSITION):SV_POSITION{return p;}\n"
                                 "float4 PSMain():SV_TARGET{return Value();}\n";
        const std::string value1 = "float4 Value(){return float4(1,0,0,1);}\n";
        const std::string value2 = "float4 Value(){return float4(0,1,0,1);}\n";
        std::filesystem::create_directories(game / "Assets");
        write(game / "WP1.project.json", Engine::Json{{"engineRoot", "../Engine"}}.dump());
        write(common / "Shaders/Mesh.hlsl", main);
        write(common / "Shaders/Mesh.hlsl.meta", Engine::Json{{"id", id}}.dump());
        write(common / "Shaders/Common/Value.hlsli", value1);
        write(common / "Shaders/Common/Value.hlsli.meta", Engine::Json{{"id", std::string(32, '2')}}.dump());
        write(common / "Assets/Fonts/Test.ttf", "font fixture");
        write(common / "Assets/Fonts/Test.ttf.meta", Engine::Json{{"id", std::string(32, '3')}}.dump());
        const auto shader = game / "Shaders/Mesh.hlsl";
        require(ContentFiles::IsShared(shader) && ContentFiles::Resolve(shader) == common / "Shaders/Mesh.hlsl",
                "Shared shaders resolve without local copies");
        require(AssetDatabase::Read(shader).id == id &&
                    AssetDatabase(game).Resolve("Shaders/Mesh.hlsl", id) == "Shaders/Mesh.hlsl",
                "Shared asset IDs retain project-relative paths");
        Editor::ProjectCatalog catalog;
        require(catalog.Scan(game), "Catalog includes shared content");
        require(catalog.Assets().size() == 3, "Shared shaders and fonts are listed exactly once");
        const auto info = Editor::AssetInfo::Read(game, {"Shaders/Mesh.hlsl", Editor::AssetKind::Shader});
        require(info.error.empty() && info.text == main, "Shared source preview reads canonical bytes");
        bool rejected = false;
        try
        {
            AssetDatabase::Write(shader, AssetDatabase::Read(shader));
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        require(rejected && !std::filesystem::exists(shader), "Shared metadata cannot be written through the game");
        SceneRuntime::MaterialAsset material;
        material.shader = "Shaders/Mesh.hlsl";
        material.Save(game, "Assets/Materials/Common.mat");
        require(SceneRuntime::MaterialAsset::Load(game, "Assets/Materials/Common.mat").shader == material.shader,
                "Materials save references to shared shaders without copying them");
        write(common / "Shaders/MissingMetadata.hlsl", main);
        require(!catalog.Scan(game) && !std::filesystem::exists(common / "Shaders/MissingMetadata.hlsl.meta"),
                "Scanning a game never writes metadata into canonical content");
        std::filesystem::remove(common / "Shaders/MissingMetadata.hlsl");
        std::string revision1, revision2;
        require(Engine::GetShaderRevision(shader, revision1), "Preprocess shared shader and include");
        rejected = false;
        try
        {
            static_cast<void>(Editor::AssetChanges::Capture(fixture / "MissingProject"));
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        require(rejected, "The asset watcher reports missing project roots");
        const auto observed = Editor::AssetChanges::Capture(game);
        const auto time = std::filesystem::last_write_time(common / "Shaders/Common/Value.hlsli");
        write(common / "Shaders/Common/Value.hlsli", value2);
        std::filesystem::last_write_time(common / "Shaders/Common/Value.hlsli", time);
        require(observed != Editor::AssetChanges::Capture(game),
                "Watch shared includes despite equal timestamp and size");
        require(Engine::GetShaderRevision(shader, revision2) && revision1 != revision2,
                "Invalidate shared include cache");
        ContentFiles::Override(game, "Shaders/Mesh.hlsl");
        ContentFiles::Override(game, "Shaders/Common/Value.hlsli");
        write(game / "Shaders/Common/Value.hlsli", value1);
        require(!ContentFiles::IsShared(shader) && AssetDatabase::Read(shader).id == id,
                "Game overrides preserve shared IDs");
        require(Engine::GetShaderRevision(shader, revision1) && revision1 != revision2,
                "Game includes take priority over common includes");
        Microsoft::WRL::ComPtr<ID3DBlob> compiled;
        require(Engine::CompileShader(shader, "PSMain", "ps_5_0", compiled),
                "Compile game override with shared layout");
        require(catalog.Scan(game) && catalog.Assets().size() == 4, "Do not duplicate overridden catalog entries");
        rejected = false;
        try
        {
            AssetDatabase(game).Move("Shaders/Mesh.hlsl", "Shaders/Renamed.hlsl");
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        require(rejected && std::filesystem::exists(shader), "An override cannot duplicate its ID through a rename");
        write(game / "Shaders/Mesh.hlsl.meta", Engine::Json{{"id", std::string(32, '4')}}.dump());
        rejected = false;
        try
        {
            static_cast<void>(AssetDatabase(game));
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        require(rejected, "Reject overrides with incompatible asset IDs");
        std::filesystem::remove(game / "Shaders/Mesh.hlsl.meta");
        std::filesystem::remove(shader);
        require(ContentFiles::IsShared(shader) && AssetDatabase::Read(shader).id == id,
                "Removing a game override restores the canonical asset");
    }
}
