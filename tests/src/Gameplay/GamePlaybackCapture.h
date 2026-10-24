#pragma once
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <Engine/Graphics/Resources/RenderTexture.h>
#include <fstream>
#include <algorithm>
#include <array>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <SceneRuntime/Scene/SceneEnvironment.h>
namespace GamePlaybackCapture
{
    /// <summary>RGBA の全画素を RGB に詰め、行単位で出力して最大の色成分を返します。</summary>
    template <bool MeasureBrightness = false>
    inline unsigned char WriteRgbRows(std::ostream& output, const unsigned char* pixels, unsigned int width,
                                      unsigned int height, size_t pitch)
    {
        std::vector<unsigned char> row(static_cast<size_t>(width) * 3);
        unsigned char brightest = 0;
        for (unsigned int y = 0; y < height; ++y)
        {
            const auto* source = pixels + y * pitch;
            auto* destination = row.data();
            for (unsigned int x = 0; x < width; ++x)
            {
                destination[0] = source[0];
                destination[1] = source[1];
                destination[2] = source[2];
                if constexpr (MeasureBrightness)
                {
                    brightest = std::max(brightest, source[0]);
                    brightest = std::max(brightest, source[1]);
                    brightest = std::max(brightest, source[2]);
                }
                source += 4;
                destination += 3;
            }
            output.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size()));
        }
        return brightest;
    }

    /// <summary>行末の余白とアルファを除いた全画素が従来の出力と一致することを確認します。</summary>
    inline void ValidateRgbRows()
    {
        const std::array<unsigned char, 32> pixels{0,   1,   2,   255, 3,   4,   5,   255, 6,   7,  8,
                                                   255, 255, 255, 255, 255, 9,   10,  11,  255, 12, 13,
                                                   14,  255, 15,  16,  17,  255, 255, 255, 255, 255};
        std::ostringstream reference, actual, measured;
        for (size_t y = 0; y < 2; ++y)
            for (size_t x = 0; x < 3; ++x)
                reference.write(reinterpret_cast<const char*>(pixels.data() + y * 16 + x * 4), 3);
        WriteRgbRows(actual, pixels.data(), 3, 2, 16);
        const auto brightest = WriteRgbRows<true>(measured, pixels.data(), 3, 2, 16);
        if (actual.str() != reference.str() || measured.str() != reference.str() || brightest != 17)
            throw std::runtime_error("row capture preserves every RGB byte and excludes alpha/padding");
    }

    /// <summary>実際の描画を保存し、必要に応じて遷移完了時の全画面の暗転を確認します。</summary>
    inline void Capture(Engine::DirectX12Renderer& renderer, SceneRuntime::SceneEnvironment& scene, unsigned int width,
                        unsigned int height, const std::string& name, bool expectBlack = false)
    {
        const auto require = [](bool ok, const char* message)
        {
            if (!ok)
                throw std::runtime_error(message);
        };
        Engine::RenderTexture target;
        require(target.Resize(renderer, width, height), "title capture target");
        const UINT pitch = (width * 4 + 255) & ~255u;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC description{};
        description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        description.Width = static_cast<UINT64>(pitch) * height;
        description.Height = 1;
        description.DepthOrArraySize = 1;
        description.MipLevels = 1;
        description.SampleDesc.Count = 1;
        description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        Microsoft::WRL::ComPtr<ID3D12Resource> readback;
        require(SUCCEEDED(renderer.GetDevice()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
                                                                        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                                        IID_PPV_ARGS(&readback))),
                "title capture readback");
        require(renderer.Render({0, 0, 0, 1},
                                [&](ID3D12GraphicsCommandList* commands, float)
                                {
                                    require(target.Begin(commands, {0, 0, 0, 1}), "title capture begins");
                                    scene.Draw(commands, width, height);
                                    require(target.End(commands), "title capture ends");
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
                                    destination.PlacedFootprint.Footprint = {Engine::RenderTexture::Format, width,
                                                                             height, 1, pitch};
                                    commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
                                    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
                                    commands->ResourceBarrier(1, &barrier);
                                }) != Engine::RenderResult::Failed &&
                    renderer.WaitForIdle(),
                "title capture GPU complete");
        unsigned char* pixels = nullptr;
        const D3D12_RANGE range{0, static_cast<SIZE_T>(pitch) * height};
        require(SUCCEEDED(readback->Map(0, &range, reinterpret_cast<void**>(&pixels))), "title capture maps");
        const auto directory = std::filesystem::absolute("generated/tests/playback-captures");
        std::filesystem::create_directories(directory);
        std::ofstream output(directory / (name + ".ppm"), std::ios::binary);
        output << "P6\n" << width << " " << height << "\n255\n";
        const auto brightest = WriteRgbRows<true>(output, pixels, width, height, pitch);
        const D3D12_RANGE empty{0, 0};
        readback->Unmap(0, &empty);
        require(static_cast<bool>(output), "title capture saved");
        require(!expectBlack || brightest <= 3, "departure completes with a fully black frame");
    }

}
