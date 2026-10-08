#pragma once
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <SceneRuntime/SceneEnvironment.h>
#include <algorithm>
#include <cmath>
namespace ProfilerValidation {
inline void Run(Engine::DirectX12Renderer& renderer) {
    Engine::CpuProfiler::Take();
    { Engine::CpuScope parent("test parent"); { Engine::CpuScope child("test child"); } { Engine::CpuScope child("test child"); } }
    const auto scopes=Engine::CpuProfiler::Take();
    const auto child=std::find_if(scopes.begin(),scopes.end(),[](const auto& s){return s.name=="test child";});
    if(child==scopes.end() || child->calls!=2 || child->milliseconds<0 || !Engine::CpuProfiler::Take().empty()) throw std::runtime_error("CPU scopes aggregation/reset failed");
    const auto content=std::filesystem::absolute("Content"); auto scene=SceneRuntime::SceneLayout::Load(content/"Assets/Scenes/RootMotionPlayground.json",content);
    scene.settings.postEffects.enabled=true; SceneRuntime::SceneEnvironment environment; std::string error;
    if(!environment.Initialize(renderer,content,scene,error)) throw std::runtime_error(error);
    for(int frame=0;frame<6;++frame) {
        environment.Update(.016,true,true);
        if(renderer.Render({0,0,0,1},[&](auto* commands,float){ environment.Draw(commands,64,32); })==Engine::RenderResult::Failed) throw std::runtime_error("profiled scene render failed");
    }
    const auto& detailed=renderer.Telemetry();
    for(const auto* name:{"Sky","Opaque meshes","Post effects","Game UI"})
        if(std::none_of(detailed.gpuPasses.begin(),detailed.gpuPasses.end(),[&](const auto& s){return s.name==name && s.calls>0 && std::isfinite(s.milliseconds) && s.milliseconds>=0;})) throw std::runtime_error("GPU pass sample missing");
    if(std::none_of(detailed.cpuScopes.begin(),detailed.cpuScopes.end(),[](const auto& s){return s.name=="Animator evaluation" && s.calls>0;})) throw std::runtime_error("runtime CPU scope missing");
    for(int frame=0;frame<4;++frame) if(renderer.Render({0,0,0,1},[&](auto* commands,float){for(int i=0;i<70;++i){Engine::GpuScope scope(commands,"capacity test");}})==Engine::RenderResult::Failed) throw std::runtime_error("GPU scope capacity render failed");
    const auto& bounded=renderer.Telemetry();
    const auto limit=std::find_if(bounded.gpuPasses.begin(),bounded.gpuPasses.end(),[](const auto& s){return s.name=="capacity test";});
    if(limit==bounded.gpuPasses.end() || limit->calls!=Engine::GpuProfileContext::Capacity) throw std::runtime_error("GPU scope capacity not enforced");
    for (int frame=0;frame<4;++frame) if (renderer.Render({0.02f,0.02f,0.02f,1})==Engine::RenderResult::Failed) throw std::runtime_error("profiler render failed");
    const auto& sample=renderer.Telemetry();
    if (sample.frames==0 || !std::isfinite(sample.cpuRenderMilliseconds) || sample.cpuRenderMilliseconds<=0 ||
        !sample.gpuSample || !std::isfinite(sample.gpuMilliseconds) || sample.gpuMilliseconds<0) throw std::runtime_error("CPU/GPU telemetry invalid");
    if(std::any_of(sample.gpuPasses.begin(),sample.gpuPasses.end(),[](const auto& s){return s.name=="capacity test" || s.name=="Opaque meshes";})) throw std::runtime_error("GPU passes retained stale frame samples");
    if (!renderer.WaitForIdle()) throw std::runtime_error("profiler completion failed");
}
}
