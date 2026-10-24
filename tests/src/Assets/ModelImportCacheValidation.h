#pragma once
#include <Engine/Graphics/Models/ModelLoader.h>
#include <Engine/Core/FileContents.h>
#include <Engine/Core/Profiler.h>
#include <algorithm>
#include <stdexcept>

namespace ModelImportCacheValidation
{
    /// <summary>静的メッシュの共有と、関連ファイル・設定変更時の再取り込みを検証します。</summary>
    inline void Run()
    {
        const auto require = [](bool ok, const char* message)
        {
            if (!ok)
                throw std::runtime_error(message);
        };
        const auto root = std::filesystem::absolute("generated/tests/model-import-cache") /
                          (std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
        std::filesystem::create_directories(root);
        const auto path = root / "Triangle.obj";
        const auto material = root / "Triangle.mtl";
        const auto metadata = root / "Triangle.obj.meta";
        const std::string source = "mtllib Triangle.mtl\nusemtl Color\nv 0 0 0\nv 1 0 0\nv 0 1 0\n"
                                   "vt 0 0\nvt 1 0\nvt 0 1\nf 1/1 2/2 3/3\n";
        Engine::FileContents::Write(path, source);
        Engine::FileContents::Write(material, "newmtl Color\nKd 0.2 0.3 0.4\n");
        std::vector<Engine::MeshData> meshes;
        const auto load = [&]
        {
            require(Engine::ModelLoader::Load(path, meshes) && meshes.size() == 1, "model cache fixture loads");
        };
        const auto count = [](const char* name)
        {
            unsigned int result = 0;
            for (const auto& sample : Engine::CpuProfiler::Take())
                if (sample.name == name)
                    result += sample.calls;
            return result;
        };
        load();
        count("Model import");
        meshes.front().vertices.front().position[0] = 999;
        load();
        require(count("Model import cache hit") == 1 && meshes.front().vertices.front().position[0] != 999,
                "unchanged model reuses an independent CPU mesh copy");
        const auto stamp = std::filesystem::last_write_time(material);
        Engine::FileContents::Write(material, "newmtl Color\nKd 0.8 0.3 0.4\n");
        std::filesystem::last_write_time(material, stamp);
        load();
        require(count("Model import") == 1 && meshes.front().vertices.front().color[0] > .7f,
                "same-size material edit with restored timestamp invalidates imported colors");
        auto changed = source;
        changed.replace(changed.find("v 1 0 0"), 7, "v 2 0 0");
        const auto modelStamp = std::filesystem::last_write_time(path);
        Engine::FileContents::Write(path, changed);
        std::filesystem::last_write_time(path, modelStamp);
        load();
        require(count("Model import") == 1 &&
                    std::any_of(meshes.front().vertices.begin(), meshes.front().vertices.end(),
                                [](const auto& vertex)
                                {
                                    return vertex.position[0] == 2;
                                }),
                "same-size geometry edit with restored timestamp invalidates imported vertices");
        Engine::FileContents::Write(metadata, "{\"id\":\"11111111111111111111111111111111\",\"scale\":3}");
        load();
        require(count("Model import") == 1 &&
                    std::any_of(meshes.front().vertices.begin(), meshes.front().vertices.end(),
                                [](const auto& vertex)
                                {
                                    return vertex.position[0] == 6;
                                }),
                "import scale change invalidates geometry");
        std::filesystem::remove(material);
        load();
        require(count("Model import") == 1, "deleted material invalidates the imported model");
        Engine::FileContents::Write(material, "newmtl Color\nKd 0.1 0.3 0.4\n");
        load();
        require(count("Model import") == 1 && meshes.front().vertices.front().color[0] < .2f,
                "newly added optional material invalidates the imported model");
        Engine::FileContents::Write(path, "invalid model");
        require(!Engine::ModelLoader::Load(path, meshes), "invalid model cannot fall back to cached geometry");
        require(meshes.front().vertices.front().color[0] < .2f, "failed import preserves the caller output");
        std::vector<std::filesystem::path> workingSet;
        for (size_t index = 0; index < 128; ++index)
        {
            const auto model = root / ("WorkingSet-" + std::to_string(index) + ".obj");
            Engine::FileContents::Write(model, source);
            require(Engine::ModelLoader::Load(model, meshes), "working set fixture imports");
            workingSet.push_back(model);
        }
        count("Model import");
        for (const auto& model : workingSet)
            require(Engine::ModelLoader::Load(model, meshes), "working set fixture reloads");
        require(count("Model import cache hit") == workingSet.size(),
                "working sets larger than 32 models do not flush the whole import cache");
        require(Engine::ModelLoader::Load(workingSet.front(), meshes), "oldest fixture becomes recently used");
        const auto extra = root / "Extra.obj";
        Engine::FileContents::Write(extra, source);
        require(Engine::ModelLoader::Load(extra, meshes), "capacity overflow fixture imports");
        count("Model import");
        require(Engine::ModelLoader::Load(workingSet.front(), meshes), "recent fixture survives capacity overflow");
        require(count("Model import cache hit") == 1, "overflow evicts only the least recently used entry");
        require(Engine::ModelLoader::Load(workingSet[1], meshes), "evicted fixture imports again");
        require(count("Model import") == 1, "least recently used entry is actually evicted");
    }
}
