#pragma once
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <Engine/Graphics/Renderers/MeshResources.h>
#include <Engine/Core/Profiler.h>
#include <Engine/Core/FileContents.h>
#include <stdexcept>
#include <algorithm>

namespace PipelineReuseValidation
{
    /// <summary>所有者の破棄後の再利用、シェーダー変更、明示解放と現存所有者の独立性を検証します。</summary>
    inline void Run(Engine::DirectX12Renderer& renderer, const std::filesystem::path& root)
    {
        const auto require = [](bool value)
        {
            if (!value)
                throw std::runtime_error("pipeline reuse validation failed");
        };
        const auto shader = root / "pipeline-reuse.hlsl";
        const auto original = Engine::FileContents::Read("Engine/Shaders/Mesh.hlsl");
        require(original.has_value());
        // include の相対位置を維持した専用ディレクトリで、他の検証素材を変更しません。
        std::filesystem::create_directories(root);
        std::filesystem::copy("Engine/Shaders", root,
                              std::filesystem::copy_options::recursive |
                                  std::filesystem::copy_options::overwrite_existing);
        Engine::FileContents::Write(shader, *original);
        Engine::MeshResources::ClearPipelineCache(renderer.GetDevice());
        const auto initialize = [&](Engine::MeshResources& resources)
        {
            require(resources.Initialize(renderer.GetDevice(), shader, renderer.GetCommandQueue()));
        };
        {
            Engine::MeshResources first;
            initialize(first);
        }
        Engine::CpuProfiler::Take();
        Engine::MeshResources reused;
        initialize(reused);
        auto samples = Engine::CpuProfiler::Take();
        require(std::none_of(samples.begin(), samples.end(),
                             [](const auto& sample)
                             {
                                 return sample.name == "Mesh pipeline creation";
                             }));
        require(std::any_of(samples.begin(), samples.end(),
                            [](const auto& sample)
                            {
                                return sample.name == "Mesh pipeline cache hit";
                            }));
        Engine::FileContents::Write(shader, *original + "\nfloat PipelineCacheRevision() { return 1; }\n");
        Engine::MeshResources changed;
        initialize(changed);
        samples = Engine::CpuProfiler::Take();
        require(std::any_of(samples.begin(), samples.end(),
                            [](const auto& sample)
                            {
                                return sample.name == "Mesh pipeline creation";
                            }));
        require(reused.GetPipelineState() != changed.GetPipelineState());
        Engine::MeshResources::ClearPipelineCache(renderer.GetDevice());
        require(reused.GetPipelineState() && changed.GetPipelineState());
        Engine::MeshResources cleared;
        initialize(cleared);
        samples = Engine::CpuProfiler::Take();
        require(std::any_of(samples.begin(), samples.end(),
                            [](const auto& sample)
                            {
                                return sample.name == "Mesh pipeline creation";
                            }));
        require(renderer.WaitForIdle());
    }
}
