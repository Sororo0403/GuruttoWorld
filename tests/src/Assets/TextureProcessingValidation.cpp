#include <Engine/Assets/TextureImport.h>
#include <DirectXTex.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    void ValidateFiltering()
    {
        Engine::TextureImportSettings settings;
        settings.mipmaps = true;
        Engine::TextureLevel pair{2, 1, {0, 0, 0, 0, 255, 255, 255, 255}};
        const auto linear = Engine::TextureImport::Build(pair, settings);
        Require(linear.size() == 2 && linear.back().pixels[0] == 128 && linear.back().pixels[3] == 128,
                "linear color and alpha mip filtering");
        settings.srgb = true;
        const auto srgb = Engine::TextureImport::Build(pair, settings);
        Require(srgb.back().pixels[0] >= 187 && srgb.back().pixels[0] <= 188 && srgb.back().pixels[3] == 128,
                "sRGB filtering averages linear light and preserves linear alpha");
        const Engine::TextureLevel odd{7, 3, std::vector<unsigned char>(7 * 3 * 4, 255)};
        const auto oddMips = Engine::TextureImport::Build(odd, settings);
        Require(oddMips.size() == 3 && oddMips[1].width == 3 && oddMips[1].height == 1 && oddMips[2].width == 1,
                "odd mip dimensions reach one pixel");
        settings.maxSize = 4;
        const auto resized = Engine::TextureImport::Build(odd, settings);
        Require(resized.front().width == 4 && resized.front().height == 1, "size limit retains aspect ratio");
        settings.compression = "bc3";
        settings.maxSize = 16384;
        const auto fallback = Engine::TextureImport::Build(odd, settings);
        Require(fallback.front().pixels.size() == odd.pixels.size(), "unaligned base keeps RGBA compatibility");
        settings.mipmaps = false;
        Require(Engine::TextureImport::Build(pair, settings).size() == 1, "single level import");
    }

    void ValidateCompression()
    {
        Engine::TextureLevel source{16, 8, std::vector<unsigned char>(16 * 8 * 4)};
        for (unsigned int y = 0; y < source.height; ++y)
        {
            for (unsigned int x = 0; x < source.width; ++x)
            {
                const auto offset = (size_t(y) * source.width + x) * 4;
                source.pixels[offset] = static_cast<unsigned char>(x * 17);
                source.pixels[offset + 1] = static_cast<unsigned char>(y * 32);
                source.pixels[offset + 2] = 64;
                source.pixels[offset + 3] = static_cast<unsigned char>((x + y) * 11);
            }
        }
        Engine::TextureImportSettings settings{true, false, 16384, "bc3"};
        const auto levels = Engine::TextureImport::Build(source, settings);
        Require(levels.size() == 5 && levels.front().pixels.size() == 128 && levels.back().pixels.size() == 16,
                "BC3 contains every mip including sub-block tails");
        for (const auto& level : levels)
        {
            const auto rowPitch = size_t((level.width + 3) / 4) * 16;
            const DirectX::Image image{level.width,           level.height,
                                       DXGI_FORMAT_BC3_UNORM, rowPitch,
                                       level.pixels.size(),   const_cast<unsigned char*>(level.pixels.data())};
            DirectX::ScratchImage decoded;
            Require(SUCCEEDED(DirectX::Decompress(image, DXGI_FORMAT_R8G8B8A8_UNORM, decoded)),
                    "every packed BC3 mip decodes through the library");
            Require(decoded.GetMetadata().width == level.width && decoded.GetMetadata().height == level.height,
                    "decoded mip retains dimensions");
            if (level.width == source.width)
            {
                double squaredError = 0;
                for (size_t i = 0; i < source.pixels.size(); ++i)
                {
                    const auto difference = double(decoded.GetPixels()[i]) - source.pixels[i];
                    squaredError += difference * difference;
                }
                Require(std::sqrt(squaredError / source.pixels.size()) < 20,
                        "gradient color and alpha compression quality");
            }
        }
        settings.maxSize = 4;
        const auto resized = Engine::TextureImport::Build(source, settings);
        Require(resized.front().width == 4 && resized.front().height == 2 && resized.front().pixels.size() == 32,
                "resize to unaligned dimensions retains RGBA fallback");
    }

    void ValidateRejection()
    {
        const Engine::TextureLevel source{1, 1, {0, 0, 0, 255}};
        for (const auto& settings : {Engine::TextureImportSettings{false, false, 0, "none"},
                                     Engine::TextureImportSettings{false, false, 16384, "unknown"}})
        {
            bool rejected = false;
            try
            {
                static_cast<void>(Engine::TextureImport::Build(source, settings));
            }
            catch (const std::exception&)
            {
                rejected = true;
            }
            Require(rejected, "invalid settings are rejected");
        }
        bool rejected = false;
        try
        {
            static_cast<void>(Engine::TextureImport::Build({2, 2, {0}}, {}));
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        Require(rejected, "invalid source buffer is rejected");
    }
}

int main()
{
    try
    {
        ValidateFiltering();
        ValidateCompression();
        ValidateRejection();
        std::cout << "PASS: DirectXTex filtering, mip tails, BC3 decoding and validation\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
