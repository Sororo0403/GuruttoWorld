#pragma once
#include "../Support/TestGameContent.h"
#include "../Animation/SkinningValidation.h"
#include <Engine/Assets/AssetDatabase.h>
#include <Engine/Graphics/Renderers/SpriteRenderer.h>
#include <Engine/Graphics/Resources/TextureManager.h>
#include <Engine/Core/Profiler.h>
#include <Engine/Core/FileContents.h>
#include <Engine/Graphics/Renderers/MeshResources.h>
#include <SceneRuntime/Rendering/MaterialTextureCache.h>
namespace TextureImportValidation
{
    using EnvironmentValidation::Require;
    /// <summary>画像変換結果の共有と、画素・寸法・全取り込み設定の違いを検証します。</summary>
    inline void Cache(Engine::DirectX12Renderer& renderer)
    {
        using namespace Engine;
        const auto calls = [](const char* name)
        {
            unsigned int count = 0;
            for (const auto& sample : CpuProfiler::Take())
                if (sample.name == name)
                    count += sample.calls;
            return count;
        };
        TextureImportSettings settings;
        settings.mipmaps = true;
        std::vector<unsigned char> pixels(32 * 16 * 4);
        for (size_t index = 0; index < pixels.size(); index += 4)
        {
            pixels[index] = 23;
            pixels[index + 1] = 45;
            pixels[index + 2] = 67;
            pixels[index + 3] = 255;
        }
        const auto load = [&](const auto& source, const auto& configuration, UINT width = 32, UINT height = 16)
        {
            auto texture = std::make_shared<Texture2D>();
            Require(texture->InitializePixels(renderer.GetDevice(), renderer.GetCommandQueue(), width, height, source,
                                              configuration),
                    "texture cache fixture uploads");
            return texture;
        };
        calls("Texture mip build");
        const auto first = load(pixels, settings);
        Require(calls("Texture mip build") == 1, "initial mip chain is built");
        const auto second = load(pixels, settings);
        Require(calls("Texture mip cache hit") == 1, "identical pixels and settings reuse converted levels");
        const auto capture = [&](const auto& texture, bool minify = false)
        {
            SpriteRenderer sprite;
            Require(sprite.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), texture,
                                      std::filesystem::absolute(WP1_TEST_GAME_ROOT "/Shaders/Sprite.hlsl")),
                    "cache verification sprite initializes");
            SpriteDrawParameters draw;
            draw.size = minify ? std::array<float, 2>{1, 1} : std::array<float, 2>{32, 16};
            return SkinningValidation::Capture(renderer,
                                               [&](auto* commands)
                                               {
                                                   sprite.Draw(commands, 64, 32, draw);
                                               });
        };
        Require(first->GetMipLevels() == 6 && capture(first) == capture(second) &&
                    capture(first, true) == capture(second, true),
                "cached mip chain renders exactly like the first conversion");
        auto revised = pixels;
        for (size_t index = 0; index < revised.size(); index += 4)
            revised[index] = 123;
        const auto changed = load(revised, settings);
        Require(calls("Texture mip build") == 1 && capture(changed) != capture(first),
                "changed pixels rebuild and reach the GPU");
        const auto restored = load(pixels, settings);
        Require(calls("Texture mip cache hit") == 1 && capture(restored) == capture(first),
                "caller pixel edits do not mutate cached input or output");
        auto srgb = settings;
        srgb.srgb = true;
        const auto gamma = load(pixels, srgb);
        Require(calls("Texture mip build") == 1 && gamma->GetFormat() == DXGI_FORMAT_R8G8B8A8_UNORM,
                "sRGB filtering setting has its own converted levels");
        auto limited = settings;
        limited.maxSize = 8;
        const auto limitedTexture = load(pixels, limited);
        Require(calls("Texture mip build") == 1 && limitedTexture->GetWidth() == 8 && limitedTexture->GetHeight() == 4,
                "size limit has its own converted dimensions");
        auto compressed = settings;
        compressed.compression = "bc3";
        const auto blocks = load(pixels, compressed);
        Require(calls("Texture mip build") == 1 && blocks->GetFormat() == DXGI_FORMAT_BC3_UNORM,
                "compression setting has its own converted blocks");
        const auto reshaped = load(pixels, settings, 16, 32);
        Require(calls("Texture mip build") == 1 && reshaped->GetWidth() == 16 && reshaped->GetHeight() == 32,
                "equal byte counts with different dimensions do not share converted levels");
        auto single = settings;
        single.mipmaps = false;
        Require(load(pixels, single)->GetMipLevels() == 1, "disabling mipmaps preserves a single level");
        auto invalid = settings;
        invalid.maxSize = 0;
        Texture2D rejected;
        Require(!rejected.InitializePixels(renderer.GetDevice(), renderer.GetCommandQueue(), 32, 16, pixels, invalid) &&
                    rejected.GetMipLevels() == 0,
                "invalid settings cannot reuse a cached conversion");
        Require(renderer.WaitForIdle(), "texture cache verification GPU completes");
    }
    inline void Run(Engine::DirectX12Renderer& renderer)
    {
        using namespace Engine;
        Cache(renderer);
        TextureLevel odd{7, 3, std::vector<unsigned char>(7 * 3 * 4, 255)};
        TextureImportSettings settings;
        settings.mipmaps = true;
        const auto oddLevels = TextureImport::Build(odd, settings);
        Require(oddLevels.size() == 3 && oddLevels[1].width == 3 && oddLevels[1].height == 1 && oddLevels[2].width == 1,
                "odd texture mip chain reaches one pixel");
        settings.maxSize = 4;
        const auto limited = TextureImport::Build(odd, settings);
        Require(limited[0].width == 4 && limited[0].height == 1, "texture size limit preserves aspect ratio");
        TextureLevel pair{2, 1, {0, 0, 0, 255, 255, 255, 255, 255}};
        settings.maxSize = 16384;
        settings.srgb = true;
        const auto linear = TextureImport::Build(pair, settings);
        Require(linear.back().pixels[0] >= 187 && linear.back().pixels[0] <= 188,
                "sRGB mip filter averages linear light");
        settings.srgb = false;
        Require(TextureImport::Build(pair, settings).back().pixels[0] == 128,
                "linear data mip filter preserves arithmetic average");
        bool invalid = false;
        try
        {
            settings.compression = "bad";
            TextureImport::Build(pair, settings);
        }
        catch (const std::exception&)
        {
            invalid = true;
        }
        Require(invalid, "unknown texture compression rejected");
        settings.compression = "bc3";
        std::vector<unsigned char> red(8 * 8 * 4);
        for (size_t i = 0; i < red.size(); i += 4)
        {
            red[i] = red[i + 3] = 255;
        }
        const auto blocks = TextureImport::Build({8, 8, red}, settings);
        Require(blocks.size() == 4 && blocks[0].pixels.size() == 64 && blocks.back().pixels.size() == 16,
                "BC3 blocks include small mip tail");
        auto compressed = std::make_shared<Texture2D>(), plain = std::make_shared<Texture2D>();
        Require(compressed->InitializePixels(renderer.GetDevice(), renderer.GetCommandQueue(), 8, 8, red, settings),
                "BC3 mip texture uploads");
        Require(compressed->GetFormat() == DXGI_FORMAT_BC3_UNORM && compressed->GetMipLevels() == 4,
                "BC3 resource exposes full mip chain");
        settings.compression = "none";
        Require(plain->InitializePixels(renderer.GetDevice(), renderer.GetCommandQueue(), 8, 8, red, settings),
                "RGBA mip texture uploads");
        SpriteRenderer first, second;
        const auto shader = std::filesystem::absolute(WP1_TEST_GAME_ROOT "/Shaders/Sprite.hlsl");
        Require(first.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), compressed, shader) &&
                    second.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), plain, shader),
                "texture comparison sprites initialize");
        SpriteDrawParameters draw;
        draw.position = {16, 8};
        draw.size = {8, 8};
        const auto actual = SkinningValidation::Capture(renderer,
                                                        [&](auto* commands)
                                                        {
                                                            first.Draw(commands, 64, 32, draw);
                                                        });
        const auto expected = SkinningValidation::Capture(renderer,
                                                          [&](auto* commands)
                                                          {
                                                              second.Draw(commands, 64, 32, draw);
                                                          });
        Require(actual == expected && actual[8 * 256 + 16 * 4] == 255,
                "BC3 GPU decompression matches reference pixels");
        draw.size = {1, 1};
        Require(SkinningValidation::Capture(renderer,
                                            [&](auto* commands)
                                            {
                                                first.Draw(commands, 64, 32, draw);
                                            }) == SkinningValidation::Capture(renderer,
                                                                              [&](auto* commands)
                                                                              {
                                                                                  second.Draw(commands, 64, 32, draw);
                                                                              }),
                "GPU minification samples uploaded BC3 mip tail");
        auto alphaPixels = red;
        for (size_t i = 0; i < 64; ++i)
            alphaPixels[i * 4 + 3] = i % 2 ? 255 : 0;
        auto alphaBc = std::make_shared<Texture2D>(), alphaRgba = std::make_shared<Texture2D>();
        settings.compression = "bc3";
        Require(
            alphaBc->InitializePixels(renderer.GetDevice(), renderer.GetCommandQueue(), 8, 8, alphaPixels, settings),
            "BC3 alpha texture uploads");
        settings.compression = "none";
        Require(
            alphaRgba->InitializePixels(renderer.GetDevice(), renderer.GetCommandQueue(), 8, 8, alphaPixels, settings),
            "reference alpha texture uploads");
        SpriteRenderer alphaFirst, alphaSecond;
        Require(alphaFirst.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), alphaBc, shader) &&
                    alphaSecond.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), alphaRgba, shader),
                "alpha comparison sprites initialize");
        draw.size = {8, 8};
        Require(SkinningValidation::Capture(renderer,
                                            [&](auto* commands)
                                            {
                                                alphaFirst.Draw(commands, 64, 32, draw);
                                            }) == SkinningValidation::Capture(renderer,
                                                                              [&](auto* commands)
                                                                              {
                                                                                  alphaSecond.Draw(commands, 64, 32,
                                                                                                   draw);
                                                                              }),
                "BC3 alpha block decoding matches RGBA blending");
        settings.compression = "bc3";
        Texture2D fallback;
        Require(
            fallback.InitializePixels(renderer.GetDevice(), renderer.GetCommandQueue(), 7, 3, odd.pixels, settings) &&
                fallback.GetFormat() == DXGI_FORMAT_R8G8B8A8_UNORM,
            "non-block-aligned texture retains RGBA dimensions");
        Texture2D rejected;
        settings.maxSize = 0;
        Require(!rejected.InitializePixels(renderer.GetDevice(), renderer.GetCommandQueue(), 8, 8, red, settings) &&
                    rejected.GetWidth() == 0,
                "invalid texture setting leaves resource uninitialized");
        const auto directory =
            std::filesystem::absolute("generated/tests/texture-import/" + std::to_string(GetCurrentProcessId()) + "-" +
                                      std::to_string(GetTickCount64()));
        std::filesystem::create_directories(directory);
        const auto image = directory / "red.bmp";
        std::array<unsigned char, 54> header{};
        header[0] = 'B';
        header[1] = 'M';
        const auto word = [&](size_t offset, uint32_t value)
        {
            for (size_t byte = 0; byte < 4; ++byte)
                header[offset + byte] = static_cast<unsigned char>(value >> (byte * 8));
        };
        word(2, 310);
        word(10, 54);
        word(14, 40);
        word(18, 8);
        word(22, 8);
        header[26] = 1;
        header[28] = 32;
        word(34, 256);
        {
            std::ofstream output(image, std::ios::binary);
            output.write(reinterpret_cast<const char*>(header.data()), header.size());
            for (size_t i = 0; i < 64; ++i)
            {
                const std::array<unsigned char, 4> pixel{0, 0, 255, 255};
                output.write(reinterpret_cast<const char*>(pixel.data()), 4);
            }
        }
        auto metadata = AssetDatabase::Ensure(image);
        TextureManager manager;
        Require(manager.Initialize(renderer.GetDevice(), renderer.GetCommandQueue()), "texture cache initializes");
        const auto initial = manager.Load(image);
        Require(initial && manager.Load(image) == initial && initial->GetMipLevels() == 1,
                "texture cache reuses unchanged import");
        MeshResources meshes;
        Require(meshes.Initialize(renderer.GetDevice(), std::filesystem::absolute("Engine/Shaders/Mesh.hlsl"),
                                  renderer.GetCommandQueue()),
                "mesh texture cache initializes");
        const auto meshInitial = meshes.GetTexture(renderer.GetDevice(), renderer.GetCommandQueue(), image);
        Require(meshInitial != nullptr, "initial mesh texture loads");
        const auto material = [&]
        {
            return SceneRuntime::MaterialTextureCache::Load(renderer.GetDevice(), renderer.GetCommandQueue(), image);
        };
        const auto materialInitial = material();
        Require(material() == materialInitial, "unchanged material textures are shared");
        const auto imageTime = std::filesystem::last_write_time(image);
        const auto originalImage = *FileContents::Read(image);
        auto revisedImage = originalImage;
        revisedImage[54] = static_cast<char>(255);
        FileContents::Write(image, revisedImage);
        std::filesystem::last_write_time(image, imageTime);
        const auto revised = manager.Load(image);
        const auto meshRevised = meshes.GetTexture(renderer.GetDevice(), renderer.GetCommandQueue(), image);
        const auto materialRevised = material();
        Require(revised && revised != initial && meshRevised && meshRevised != meshInitial &&
                    materialRevised != materialInitial,
                "same-size pixel edits with restored timestamps reimport in all three texture caches");
        FileContents::Write(image, originalImage);
        std::filesystem::last_write_time(image, imageTime);
        Require(manager.Load(image) != revised &&
                    meshes.GetTexture(renderer.GetDevice(), renderer.GetCommandQueue(), image) != meshRevised &&
                    material() != materialRevised,
                "restored bytes invalidate all texture caches independently of timestamps");
        const auto metadataTime = std::filesystem::last_write_time(AssetDatabase::Sidecar(image));
        metadata.texture = {true, true, 4, "bc3"};
        AssetDatabase::Write(image, metadata);
        std::filesystem::last_write_time(AssetDatabase::Sidecar(image), metadataTime);
        const auto imported = manager.Load(image);
        const auto meshImported = meshes.GetTexture(renderer.GetDevice(), renderer.GetCommandQueue(), image);
        const auto materialImported = material();
        Require(imported && imported != initial && imported->GetWidth() == 4 && imported->GetMipLevels() == 3 &&
                    imported->GetFormat() == DXGI_FORMAT_BC3_UNORM && initial->GetWidth() == 8,
                "metadata reimport replaces cache while retaining old resource");
        Require(meshImported && meshImported->GetWidth() == 4 && meshImported->GetMipLevels() == 3 &&
                    meshImported->GetFormat() == DXGI_FORMAT_BC3_UNORM && materialImported->GetWidth() == 4 &&
                    materialImported->GetMipLevels() == 3 && materialImported->GetFormat() == DXGI_FORMAT_BC3_UNORM,
                "same-time metadata edits reimport mesh and material textures");
        const auto saved = AssetDatabase::Read(image);
        Require(saved.id == metadata.id && saved.texture.srgb && saved.texture.mipmaps && saved.texture.maxSize == 4 &&
                    saved.texture.compression == "bc3",
                "texture metadata roundtrips with stable ID");
        metadata.texture.maxSize = 0;
        invalid = false;
        try
        {
            AssetDatabase::Write(image, metadata);
        }
        catch (const std::exception&)
        {
            invalid = true;
        }
        Require(invalid && AssetDatabase::Read(image).texture.maxSize == 4,
                "invalid import setting preserves saved metadata");
        const auto validMetadata = *FileContents::Read(AssetDatabase::Sidecar(image));
        FileContents::Write(AssetDatabase::Sidecar(image), "{");
        std::filesystem::last_write_time(AssetDatabase::Sidecar(image), metadataTime);
        Require(!manager.Load(image) && !meshes.GetTexture(renderer.GetDevice(), renderer.GetCommandQueue(), image),
                "corrupt metadata cannot return stale cached textures");
        invalid = false;
        try
        {
            material();
        }
        catch (const std::exception&)
        {
            invalid = true;
        }
        Require(invalid, "corrupt metadata cannot return a stale material texture");
        FileContents::Write(AssetDatabase::Sidecar(image), validMetadata);
        std::filesystem::last_write_time(AssetDatabase::Sidecar(image), metadataTime);
        Require(manager.Load(image) == imported &&
                    meshes.GetTexture(renderer.GetDevice(), renderer.GetCommandQueue(), image) == meshImported &&
                    material() == materialImported,
                "failed reimport preserves the last valid cache entry");
    }
}
