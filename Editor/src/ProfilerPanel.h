#pragma once
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <SceneRuntime/SceneWorld.h>
#include <Engine/Assets/AssetDatabase.h>
#include <psapi.h>
#include <imgui.h>
#include <chrono>
namespace Editor {
class ProfilerPanel final {
    struct Sample { double frame=0,update=0,render=0,gpu=0; size_t draws=0,triangles=0; SIZE_T workingSet=0,privateBytes=0; UINT64 video=0,budget=0; bool gpuValid=false,videoValid=false; };
    Sample sample_;
    std::vector<Sample> samples_;
    Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter_;
    std::string status_;
    bool capture_=true,previousValid_=false;
    std::chrono::steady_clock::time_point previous_;
public:
    bool open=false;
    void Update(const Engine::DirectX12Renderer& renderer,const SceneRuntime::SceneWorld& world,double seconds,double updateMilliseconds) {
        const auto now=std::chrono::steady_clock::now();
        const double elapsed=previousValid_ ? std::chrono::duration<double,std::milli>(now-previous_).count() : seconds*1000;
        previous_=now; previousValid_=true;
        if (!capture_) return;
        sample_={}; sample_.frame=elapsed; sample_.update=updateMilliseconds;
        const auto& telemetry=renderer.Telemetry(); sample_.render=telemetry.cpuRenderMilliseconds; sample_.gpu=telemetry.gpuMilliseconds; sample_.gpuValid=telemetry.gpuSample;
        sample_.draws=world.Telemetry().draws; sample_.triangles=world.Telemetry().triangles;
        PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb=sizeof(memory);
        if (K32GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory))) { sample_.workingSet=memory.WorkingSetSize; sample_.privateBytes=memory.PrivateUsage; }
        if (!adapter_ && renderer.GetDevice()) {
            Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
            if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) factory->EnumAdapterByLuid(renderer.GetDevice()->GetAdapterLuid(),IID_PPV_ARGS(&adapter_));
        }
        DXGI_QUERY_VIDEO_MEMORY_INFO video{};
        if (adapter_ && SUCCEEDED(adapter_->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&video))) { sample_.video=video.CurrentUsage; sample_.budget=video.Budget; sample_.videoValid=true; }
        if (samples_.size()==600) samples_.erase(samples_.begin()); samples_.push_back(sample_);
    }
    void Draw(const std::filesystem::path& content) {
        if (!open) return;
        if (ImGui::Begin("性能計測###Profiler",&open)) {
            ImGui::Checkbox("記録###Capture",&capture_);
            ImGui::Text("フレーム: %.2f ms / %.1f FPS",sample_.frame,sample_.frame>0 ? 1000/sample_.frame : 0);
            ImGui::Text("CPU 更新・準備: %.3f ms",sample_.update);
            ImGui::Text("CPU 描画・Present待ち: %.3f ms",sample_.render);
            if (sample_.gpuValid) ImGui::Text("GPU 描画（完了済みフレーム）: %.3f ms",sample_.gpu);
            else ImGui::TextUnformatted("GPU: 計測待ち、または未対応");
            ImGui::Text("メッシュ描画: %zu / 三角形: %zu（影を含む）",sample_.draws,sample_.triangles);
            ImGui::Text("Working set: %.1f MiB / Private: %.1f MiB",static_cast<double>(sample_.workingSet)/1048576,static_cast<double>(sample_.privateBytes)/1048576);
            if (sample_.videoValid) ImGui::Text("GPUローカルメモリ: %.1f / %.1f MiB",static_cast<double>(sample_.video)/1048576,static_cast<double>(sample_.budget)/1048576);
            std::vector<float> times; for (const auto& sample : samples_) times.push_back(static_cast<float>(sample.frame));
            if (!times.empty()) ImGui::PlotLines("フレーム時間###Frame history",times.data(),static_cast<int>(times.size()),0,nullptr,0,50,ImVec2(0,80));
            if (ImGui::Button("CSVを書き出す###Export CSV")) try {
                const auto directory=content.parent_path()/"generated/profiler"; std::filesystem::create_directories(directory);
                const auto path=directory/("profile-"+std::to_string(GetTickCount64())+".csv"); std::ofstream output(path);
                output<<"frame_ms,cpu_update_ms,cpu_render_present_ms,gpu_ms,gpu_valid,mesh_draws,triangles,working_set_bytes,private_bytes,local_video_bytes,video_budget_bytes,video_valid\n";
                for (const auto& sample : samples_) output<<sample.frame<<','<<sample.update<<','<<sample.render<<','<<sample.gpu<<','<<sample.gpuValid<<','<<sample.draws<<','<<sample.triangles<<','<<sample.workingSet<<','<<sample.privateBytes<<','<<sample.video<<','<<sample.budget<<','<<sample.videoValid<<'\n';
                output.close(); if (!output) throw std::runtime_error("Cannot write profiler CSV"); status_=Engine::AssetDatabase::Text(path);
            } catch (const std::exception& error) { status_=error.what(); }
            if (!status_.empty()) ImGui::TextWrapped("%s",status_.c_str());
        } ImGui::End();
    }
};
}
