#include <Engine/Assets/AssetArtifact.h>
#include <Engine/Assets/AssetDatabase.h>
#include <Engine/Assets/TextureAssets.h>
#include <Engine/Animation/SkeletonAssets.h>
#include <Engine/Graphics/Models/ModelAssets.h>
#include <Engine/Core/DiagnosticPaths.h>
#include <fstream>
#include <iostream>
#include <map>

#if defined(__d3d12_h__)
#error Packaged CPU assets must not include D3D12
#endif

namespace
{
    std::filesystem::path diagnostics;
}
namespace Engine
{
    // 静的リンク時に検証専用の保存先を提供し、利用者の既存キャッシュを読まずに実ローダーを試します。
    std::filesystem::path GetDiagnosticsRoot()
    {
        return diagnostics;
    }
}
int wmain(int argc, wchar_t** argv)
{
    try
    {
        if (argc != 4)
            throw std::runtime_error("Pass package root, empty writable cache root and hit/recover mode");
        const auto root = std::filesystem::absolute(argv[1]);
        diagnostics = std::filesystem::absolute(argv[2]);
        const bool recover = std::wstring_view(argv[3]) == L"recover";
        if (std::filesystem::exists(diagnostics))
            throw std::runtime_error("CPU package cache must start empty");
        std::filesystem::create_directories(diagnostics);
        {
            std::ofstream writable(diagnostics / "writable-probe");
            writable << "probe";
            if (!writable)
                throw std::runtime_error("CPU package cache must be writable");
        }
        Engine::AssetImportScope imports;
        Engine::AssetDatabase assets(root, true);
        std::map<std::string, size_t> loaded;
        for (const auto& file : std::filesystem::recursive_directory_iterator(root / "Assets/Compiled"))
        {
            if (!file.is_regular_file() || !file.path().filename().string().ends_with(".recipe.json"))
                continue;
            const auto recipe = Engine::Json::parse(std::ifstream(file.path()));
            const auto kind = recipe.at("kind").get<std::string>();
            const auto id = recipe.at("assetId").get<std::string>();
            const auto source = id.starts_with("path:") ? Engine::AssetPath::Path(id.substr(5))
                                                        : assets.Resolve("Assets/Validation.asset", id);
            if (source.empty())
                throw std::runtime_error("Cooked asset ID cannot resolve after relocation: " + id);
            std::string error;
            bool success = false;
            if (kind == "static-mesh")
                success = bool(Engine::ModelAssets::Load(root / source, error));
            else if (kind == "skeletal-mesh")
                success = bool(Engine::SkeletonAssets::Load(root / source, error));
            else if (kind == "texture-mips")
                success = bool(Engine::TextureAssets::Load(root / source, error));
            else
                throw std::runtime_error("Unexpected cooked CPU asset kind: " + kind);
            if (!success)
                throw std::runtime_error("Relocated CPU asset load failed: " + id + " / " + error);
            ++loaded[kind];
        }
        size_t localArtifacts = 0;
        for (const auto& file : std::filesystem::recursive_directory_iterator(diagnostics))
            localArtifacts += file.is_regular_file() && file.path().extension() == ".artifact";
        if (loaded.empty() || (!recover && localArtifacts != 0) || (recover && localArtifacts == 0))
            throw std::runtime_error("Cooked cache hit or corrupt-cache regeneration was not observed");
        for (const auto& [kind, count] : loaded)
            std::cout << kind << ": " << count << '\n';
        std::cout << "PASS: relocated typed CPU loaders, cold cache, "
                  << (recover ? "corrupt cooked artifact regenerated locally" : "read-only cooked artifacts reused")
                  << "; local artifacts=" << localArtifacts << '\n';
        return 0;
    }
    catch (const std::exception& failure)
    {
        std::cerr << "FAIL: " << failure.what() << '\n';
        return 1;
    }
}
