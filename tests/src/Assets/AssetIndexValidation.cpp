#include <Engine/Assets/AssetDatabase.h>
#include <Engine/Core/FileContents.h>
#include <Engine/Core/Profiler.h>
#include <future>
#include <iostream>

namespace
{
    // 検証失敗を呼出元へ伝えます。
    void Require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    // 外部ツールと同じ通常の書き込みで検証素材を更新します。
    void Write(const std::filesystem::path& path, const std::string& source)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << source;
        output.close();
        Require(static_cast<bool>(output), "fixture write failed");
    }

    // 素材索引の実際の再構築回数を取得します。
    unsigned int Rebuilds()
    {
        unsigned int count = 0;
        for (const auto& sample : Engine::CpuProfiler::Take())
            if (sample.name == "Asset index rebuild")
                count += sample.calls;
        return count;
    }

    // 古い索引を返す代わりに不正なメタデータを拒否することを確認します。
    void Reject(const std::filesystem::path& root)
    {
        bool rejected = false;
        try
        {
            Engine::AssetDatabase index(root, true);
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        Require(rejected, "invalid index was accepted or stale cache was returned");
    }
}

int main()
{
    try
    {
        const auto fixture = std::filesystem::absolute("generated/tests/asset-index") /
                             (std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
        const auto game = fixture / "Game";
        const auto asset = game / "Assets/Textures/Test.png";
        const std::string first = "11111111111111111111111111111111";
        const std::string second = "22222222222222222222222222222222";
        const std::string third = "33333333333333333333333333333333";
        const std::string fourth = "44444444444444444444444444444444";
        // 初期保存を完了してから監視を始め、以降の外部編集は通常のWriteで再現します。
        std::filesystem::create_directories(asset.parent_path());
        Engine::FileContents::Write(asset, "fixture");
        Engine::FileContents::Write(Engine::AssetDatabase::Sidecar(asset), "{\"id\":\"" + first + "\"}");
        const auto context = Engine::ContentContext::Capture(game);
        Engine::AssetDatabase original(context, true);
        Require(Rebuilds() == 1, "initial index was not built once");
        for (int i = 0; i < 100; ++i)
        {
            Engine::AssetDatabase shared(Engine::ContentContext::Capture(game), i % 2 == 0);
            Require(shared.Resolve("Assets/Textures/Test.png", first) == "Assets/Textures/Test.png",
                    "shared index lost an ID");
        }
        const auto unchangedRebuilds = Rebuilds();
        Require(unchangedRebuilds == 0,
                ("unchanged content rebuilt the index " + std::to_string(unchangedRebuilds) + " times").c_str());

        const auto metadata = Engine::AssetDatabase::Sidecar(asset);
        const auto stamp = std::filesystem::last_write_time(metadata);
        Write(metadata, "{\"id\":\"" + second + "\"}");
        std::filesystem::last_write_time(metadata, stamp);
        Engine::AssetDatabase revised(context, true);
        Require(revised.ReferenceId("Assets/Textures/Test.png") == second &&
                    original.ReferenceId("Assets/Textures/Test.png") == first,
                "same-size timestamp-preserving edit or snapshot isolation failed");
        Require(Rebuilds() == 1, "metadata update did not rebuild exactly once");

        const auto added = game / "Assets/Textures/Added.png";
        Write(added, "added");
        Write(Engine::AssetDatabase::Sidecar(added), "{\"id\":\"" + third + "\"}");
        Require(Engine::AssetDatabase(context, true).ReferenceId("Assets/Textures/Added.png") == third,
                "new asset was not indexed");
        std::filesystem::remove(added);
        Require(Engine::AssetDatabase(context, true).ReferenceId("Assets/Textures/Added.png").empty(),
                "deleted asset remained indexed");

        Write(metadata, "broken");
        Reject(game);
        Reject(game);
        Write(metadata, "{\"id\":\"" + second + "\"}");
        Require(Engine::AssetDatabase(context, true).ReferenceId("Assets/Textures/Test.png") == second,
                "index did not recover after invalid metadata");
        Write(added, "duplicate");
        Write(Engine::AssetDatabase::Sidecar(added), "{\"id\":\"" + second + "\"}");
        Reject(game);
        Write(Engine::AssetDatabase::Sidecar(added), "{\"id\":\"" + third + "\"}");
        Engine::AssetDatabase recovered(context, true);
        Rebuilds();
        std::vector<std::future<std::filesystem::path>> readers;
        for (int i = 0; i < 8; ++i)
            readers.push_back(std::async(
                std::launch::async,
                [&]
                {
                    const auto path = Engine::AssetDatabase(context, true).Resolve("Assets/Textures/Test.png", second);
                    Require(Rebuilds() == 0, "parallel reader rebuilt unchanged index");
                    return path;
                }));
        for (auto& reader : readers)
            Require(reader.get() == "Assets/Textures/Test.png", "parallel index acquisition failed");
        Require(Rebuilds() == 0, "parallel reads invalidated unchanged content");

        const auto moved = std::filesystem::path("Assets/Textures/Moved.png");
        Engine::AssetDatabase(game).Move("Assets/Textures/Test.png", moved);
        Engine::AssetDatabase renamed(context, true);
        Require(renamed.Resolve("Assets/Textures/Test.png", second) == moved &&
                    renamed.Resolve("Assets/Textures/Test.png") == moved &&
                    recovered.Resolve("Assets/Textures/Test.png", second) == "Assets/Textures/Test.png",
                "rename aliases or frozen ID mappings were lost");

        const auto shared = fixture / "Engine/Engine/Shaders/Mesh.hlsl";
        Write(shared, "shader");
        Write(Engine::AssetDatabase::Sidecar(shared), "{\"id\":\"" + first + "\"}");
        Write(game / "WP1.project.json", "{\"engineRoot\":\"../Engine\"}");
        const auto layered = Engine::ContentContext::Capture(game);
        Require(Engine::AssetDatabase(layered, true).ReferenceId("Engine/Shaders/Mesh.hlsl") == first &&
                    Engine::AssetDatabase(context, true).ReferenceId("Engine/Shaders/Mesh.hlsl").empty(),
                "content layers leaked between contexts");
        Write(Engine::AssetDatabase::Sidecar(shared), "{\"id\":\"" + fourth + "\"}");
        Require(Engine::AssetDatabase(layered, true).ReferenceId("Engine/Shaders/Mesh.hlsl") == fourth,
                "shared layer edit was not detected");
        const auto shader = game / "Shaders/Local.hlsl";
        Write(shader, "local");
        Write(Engine::AssetDatabase::Sidecar(shader), "{\"id\":\"" + first + "\"}");
        Require(Engine::AssetDatabase(layered, true).ReferenceId("Shaders/Local.hlsl") == first,
                "new Shaders directory was not detected");
        std::filesystem::rename(game / "Shaders", game / "PreviousShaders");
        Require(Engine::AssetDatabase(layered, true).ReferenceId("Shaders/Local.hlsl").empty(),
                "removed Shaders directory remained indexed");
        std::filesystem::rename(game / "PreviousShaders", game / "Shaders");
        Require(Engine::AssetDatabase(layered, true).ReferenceId("Shaders/Local.hlsl") == first,
                "recreated Shaders directory was not watched");
        Write(fixture / "Other/Assets/Textures/Test.png", "other");
        Write(fixture / "Other/Assets/Textures/Test.png.meta", "{\"id\":\"" + third + "\"}");
        Require(Engine::AssetDatabase(fixture / "Other", true).ReferenceId("Assets/Textures/Test.png") == third &&
                    recovered.ReferenceId("Assets/Textures/Test.png") == second,
                "project isolation failed");
        std::cout
            << "PASS: shared asset index, external edits, immutable snapshots, failure recovery, concurrent reads, "
               "shared layers, directory recreation and project isolation\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
