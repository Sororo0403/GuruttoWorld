#include <Engine/Graphics/ShaderCompiler.h>
#include <Engine/Assets/AssetArtifact.h>
#include <Engine/Core/Json.h>
#include <Engine/Core/FileContents.h>
#include <chrono>
#include <fstream>
#include <iostream>

#if defined(__d3d12_h__)
#error Shader cook must not include D3D12
#endif

int main()
{
    try
    {
        const auto root = std::filesystem::absolute("generated/tests/shader-artifacts") /
                          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::create_directories(root / "Shaders");
        std::filesystem::create_directories(root / "Assets");
        std::filesystem::create_directories(root / "Engine/Shaders");
        Engine::FileContents::Write(root / "WP1.runtime.json",
                                    R"({"version":1,"projectId":"11111111111111111111111111111111"})");
        Engine::FileContents::Write(root / "Assets/Project.json", "{}");
        Engine::FileContents::Write(root / "Engine/Shaders/Shared.hlsl",
                                    "float4 SharedColor() { return float4(1,0,0,1); }\n");
        Engine::FileContents::Write(root / "Shaders/Custom.hlsl",
                                    R"(#include "Shared.hlsl"
float4 PSMain() : SV_TARGET { return SharedColor(); }
)");
        std::string error;
        if (!Engine::PrepareShaderCache(root / "Shaders", error))
            throw std::runtime_error(error);
        Engine::FileContents::Write(root / "Shaders/Shared.hlsl",
                                    *Engine::FileContents::Read(root / "Engine/Shaders/Shared.hlsl"));
        if (!Engine::PrepareShaderCache(root / "Shaders", error))
            throw std::runtime_error(error);
        std::filesystem::remove(root / "Shaders/Shared.hlsl");
        if (!Engine::PrepareShaderCache(root / "Shaders", error))
            throw std::runtime_error(error);
        size_t inputs = 0;
        bool shared = false;
        for (const auto& file : std::filesystem::directory_iterator(root / "Shaders/Compiled"))
        {
            if (!file.path().filename().string().ends_with(".recipe.json"))
                continue;
            const auto recipe = Engine::Json::parse(std::ifstream(file.path()));
            const auto binary =
                Engine::ArtifactStore(file.path().parent_path()).Read(recipe.at("key").get<std::string>());
            if (!binary || Engine::ArtifactHash::Bytes(binary->bytes) != recipe.at("outputRevision").get<std::string>())
                throw std::runtime_error("Invalid shader artifact output proof");
            for (const auto& input : recipe.at("inputs"))
            {
                const auto path = file.path().parent_path().parent_path() /
                                  std::filesystem::path(input.at("path").get<std::string>());
                if (!std::filesystem::is_regular_file(path) ||
                    Engine::ArtifactHash::File(path) != input.at("sha256").get<std::string>())
                    throw std::runtime_error("Shader recipe must name the physical input: " + path.string());
                shared |= std::filesystem::equivalent(path, root / "Engine/Shaders/Shared.hlsl");
                ++inputs;
            }
        }
        if (!shared || inputs < 2)
            throw std::runtime_error("Shared shader include was omitted from the cook proof");
        std::cout << "PASS: CPU shader cook records physical shared includes and verifiable output hashes\n";
        return 0;
    }
    catch (const std::exception& failure)
    {
        std::cerr << "FAIL: " << failure.what() << '\n';
        return 1;
    }
}
