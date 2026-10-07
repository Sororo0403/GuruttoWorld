#pragma once
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <cmath>
namespace ProfilerValidation {
inline void Run(Engine::DirectX12Renderer& renderer) {
    for (int frame=0;frame<4;++frame) if (renderer.Render({0.02f,0.02f,0.02f,1})==Engine::RenderResult::Failed) throw std::runtime_error("profiler render failed");
    const auto& sample=renderer.Telemetry();
    if (sample.frames==0 || !std::isfinite(sample.cpuRenderMilliseconds) || sample.cpuRenderMilliseconds<=0 ||
        !sample.gpuSample || !std::isfinite(sample.gpuMilliseconds) || sample.gpuMilliseconds<0) throw std::runtime_error("CPU/GPU telemetry invalid");
    if (!renderer.WaitForIdle()) throw std::runtime_error("profiler completion failed");
}
}
