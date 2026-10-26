#pragma once
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <Engine/Graphics/Resources/RenderTexture.h>
#include <functional>
#include <stdexcept>

namespace TestSupport
{
    /// <summary>読み戻しの前提を確認し、GPU描画の失敗を呼出元へ返します。</summary>
    inline void CheckPixel(bool success, const char* message)
    {
        if (!success)
            throw std::runtime_error(message);
    }
    inline std::array<unsigned char, 4> Pixel(Engine::DirectX12Renderer& renderer,
                                              const std::function<void(ID3D12GraphicsCommandList*)>& draw)
    {
        Engine::RenderTexture target;
        CheckPixel(target.Resize(renderer, 64, 32), "environment pixel target initializes");
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC description{};
        description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        description.Width = 256 * 32;
        description.Height = 1;
        description.DepthOrArraySize = 1;
        description.MipLevels = 1;
        description.SampleDesc.Count = 1;
        description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        Microsoft::WRL::ComPtr<ID3D12Resource> readback;
        CheckPixel(SUCCEEDED(renderer.GetDevice()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
                                                                           D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                                           IID_PPV_ARGS(&readback))),
                   "environment readback initializes");
        CheckPixel(
            renderer.Render({0, 0, 0, 1},
                            [&](ID3D12GraphicsCommandList* commands, float)
                            {
                                CheckPixel(target.Begin(commands, {0, 0, 0, 1}), "environment pixel target begins");
                                draw(commands);
                                CheckPixel(target.End(commands), "environment pixel target ends");
                                D3D12_RESOURCE_BARRIER barrier{};
                                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                                barrier.Transition.pResource = target.GetResource();
                                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                                barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                                barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
                                commands->ResourceBarrier(1, &barrier);
                                D3D12_TEXTURE_COPY_LOCATION source{}, destination{};
                                source.pResource = target.GetResource();
                                source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                                destination.pResource = readback.Get();
                                destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                                destination.PlacedFootprint.Footprint = {Engine::RenderTexture::Format, 64, 32, 1, 256};
                                commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
                                std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
                                commands->ResourceBarrier(1, &barrier);
                            }) != Engine::RenderResult::Failed &&
                renderer.WaitForIdle(),
            "environment pixels complete on GPU");
        unsigned char* data = nullptr;
        const D3D12_RANGE range{16 * 256 + 32 * 4, 16 * 256 + 32 * 4 + 4};
        CheckPixel(SUCCEEDED(readback->Map(0, &range, reinterpret_cast<void**>(&data))), "environment pixels map");
        const auto offset = range.Begin;
        const std::array<unsigned char, 4> result{data[offset], data[offset + 1], data[offset + 2], data[offset + 3]};
        const D3D12_RANGE empty{0, 0};
        readback->Unmap(0, &empty);
        return result;
    }
}
