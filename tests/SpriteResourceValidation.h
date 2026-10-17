#pragma once

#include <Engine/Graphics/DirectX12/GpuRetirement.h>
#include <Engine/Graphics/Renderers/SpriteRenderer.h>
#include <Engine/Graphics/Resources/TextureManager.h>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace SpriteResourceValidation
{
    /// <summary>共有パイプライン・シェーダー更新・キュー分離・退役フェンスを検証します。</summary>
    inline void Run(ID3D12Device* device, ID3D12CommandQueue* queue, const std::filesystem::path& root)
    {
        const auto require = [](bool condition, const char* message)
        {
            if (!condition)
                throw std::runtime_error(message);
        };
        Engine::TextureManager textures;
        require(textures.Initialize(device, queue), "sprite test textures");
        const auto texture = textures.Load({});
        Engine::SpriteRenderer first, second, depth;
        require(first.Initialize(device, queue, texture, root / "Shaders/Ui.hlsl"), "first sprite");
        require(second.Initialize(device, queue, texture, root / "Shaders/Ui.hlsl"), "second sprite");
        require(first.SharesPipelineWith(second), "sprites reuse mesh and PSO");
        auto changedTexture = std::make_shared<Engine::Texture2D>();
        require(changedTexture->InitializePixels(device, queue, 1, 1, {255, 0, 0, 255}), "changed sprite texture");
        Engine::SpriteRenderer rebound;
        require(rebound.InitializeShared(first, changedTexture) && first.SharesPipelineWith(rebound),
                "texture-only replacement retains immutable pipeline");
        Engine::SpriteRenderer uninitialized, invalidShared;
        require(!invalidShared.InitializeShared(uninitialized, texture), "uninitialized prototype rejected");
        require(!invalidShared.InitializeShared(first, {}), "missing replacement texture rejected");
        require(depth.Initialize(device, queue, texture, root / "Shaders/Ui.hlsl", true), "depth sprite");
        require(!first.SharesPipelineWith(depth), "depth settings isolate pipeline");
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> otherQueue;
        D3D12_COMMAND_QUEUE_DESC description{};
        require(SUCCEEDED(device->CreateCommandQueue(&description, IID_PPV_ARGS(&otherQueue))), "second queue");
        Engine::SpriteRenderer other;
        require(other.Initialize(device, otherQueue.Get(), texture, root / "Shaders/Ui.hlsl"), "other queue sprite");
        require(!first.SharesPipelineWith(other), "queues isolate mesh uploads");

        const auto folder = std::filesystem::path("generated/park/resource-validation");
        std::filesystem::create_directories(folder);
        const auto shader = folder / "ChangedUi.hlsl";
        std::filesystem::copy(root / "Shaders", folder,
                              std::filesystem::copy_options::recursive |
                                  std::filesystem::copy_options::overwrite_existing);
        std::ifstream input(root / "Shaders/Ui.hlsl", std::ios::binary);
        const std::string source((std::istreambuf_iterator<char>(input)), {});
        const auto write = [&](const std::string& text)
        {
            std::ofstream output(shader, std::ios::binary);
            output << text;
            require(bool(output), "write temporary sprite shader");
        };
        write(source);
        Engine::SpriteRenderer revision1;
        require(revision1.Initialize(device, queue, texture, shader), "first shader revision");
        write(source + "\r\nstatic const float RevisionMarker = 1.0f;\r\n");
        Engine::SpriteRenderer revision2;
        require(revision2.Initialize(device, queue, texture, shader), "changed shader revision");
        require(!revision1.SharesPipelineWith(revision2), "shader edit invalidates sprite cache");
        write("invalid shader");
        Engine::SpriteRenderer invalid;
        require(!invalid.Initialize(device, queue, texture, shader), "failed shader cannot reuse stale pipeline");
        std::filesystem::remove(shader);

        auto retirement = Engine::GpuRetirement::Shared(device, queue);
        require(retirement && retirement == Engine::GpuRetirement::Shared(device, queue),
                "same queue shares retirement");
        require(retirement != Engine::GpuRetirement::Shared(device, otherQueue.Get()),
                "different queue retirement isolated");
        Microsoft::WRL::ComPtr<ID3D12Fence> gate;
        require(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate))), "retirement gate");
        // Ensure the queue is released even if an assertion throws.
        struct GateRelease
        {
            ID3D12Fence* fence;
            ~GateRelease()
            {
                fence->Signal(1);
            }
        } release{gate.Get()};
        require(SUCCEEDED(queue->Wait(gate.Get(), 1)), "hold GPU completion");
        auto old = std::make_shared<int>(7);
        std::weak_ptr<int> weak = old;
        require(retirement->Retire(old), "retire old resource");
        old.reset();
        retirement->Collect();
        require(!weak.expired(), "in-flight resource remains alive");
        gate->Signal(1);
        Microsoft::WRL::ComPtr<ID3D12Fence> completed;
        require(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&completed))), "completion fence");
        require(Engine::SignalGpuFence(device, queue, completed.Get(), 1), "completion signal");
        require(Engine::WaitForGpuFence(device, completed.Get(), 1, nullptr), "completion wait");
        retirement->Collect();
        require(weak.expired(), "completed resource released");
    }
}
