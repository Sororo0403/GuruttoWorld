#pragma once
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <SceneRuntime/Scene/SceneWorld.h>
#include <Engine/Assets/AssetDatabase.h>
#include <psapi.h>
#include <imgui.h>
#include <chrono>
namespace Editor
{
    class ProfilerPanel final
    {
        struct Sample
        {
            double frame = 0, update = 0, render = 0, gpu = 0;
            size_t draws = 0, triangles = 0;
            SIZE_T workingSet = 0, privateBytes = 0;
            UINT64 video = 0, budget = 0;
            bool gpuValid = false, videoValid = false;
            std::vector<Engine::ProfileSample> cpuScopes, gpuPasses;
        };
        Sample sample_;
        std::vector<Sample> samples_;
        Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter_;
        std::string status_;
        bool capture_ = true, previousValid_ = false;
        std::chrono::steady_clock::time_point previous_;

    public:
        bool open = false;
        /// <summary>状態を更新します。</summary>
        void Update(const Engine::DirectX12Renderer& renderer, const SceneRuntime::SceneWorld& world, double seconds,
                    double updateMilliseconds);
        /// <summary>書き出しの編集画面を描画します。</summary>
        void DrawExport(const std::filesystem::path& content);
        /// <summary>編集画面を描画します。</summary>
        void Draw(const std::filesystem::path& content);
    };
}
