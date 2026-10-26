#include <Engine/Assets/TextureImport.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <DirectXTex.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace
{
    void Require(HRESULT result)
    {
        if (FAILED(result))
        {
            throw std::runtime_error("DirectXTex texture conversion failed: " + std::to_string(result));
        }
    }
}

namespace Engine
{
    void TextureImportSettings::Validate() const
    {
        if (!maxSize || maxSize > 16384 || (compression != "none" && compression != "bc3"))
        {
            throw std::runtime_error("Invalid texture import settings");
        }
    }

    bool TextureImport::Compressed(const TextureImportSettings& settings, const TextureLevel& source)
    {
        return settings.compression == "bc3" && source.width % 4 == 0 && source.height % 4 == 0;
    }

    std::vector<TextureLevel> TextureImport::Build(TextureLevel source, const TextureImportSettings& settings)
    {
        settings.Validate();
        if (!source.width || !source.height || source.width > 16384 || source.height > 16384 ||
            source.pixels.size() != size_t(source.width) * source.height * 4)
        {
            throw std::runtime_error("Invalid texture pixels");
        }
        const DirectX::Image input{
            source.width,         source.height,       DXGI_FORMAT_R8G8B8A8_UNORM, size_t(source.width) * 4,
            source.pixels.size(), source.pixels.data()};
        auto filter = DirectX::TEX_FILTER_TRIANGLE | DirectX::TEX_FILTER_FORCE_NON_WIC;
        if (settings.srgb)
        {
            filter |= DirectX::TEX_FILTER_SRGB;
        }
        DirectX::ScratchImage resized, mips, compressed;
        const auto* base = &input;
        if (std::max(source.width, source.height) > settings.maxSize)
        {
            const double factor = double(settings.maxSize) / std::max(source.width, source.height);
            Require(DirectX::Resize(input, std::max(1u, unsigned(source.width * factor)),
                                    std::max(1u, unsigned(source.height * factor)), filter, resized));
            base = resized.GetImage(0, 0, 0);
        }
        const DirectX::Image* images = base;
        size_t count = 1;
        if (settings.mipmaps && (base->width > 1 || base->height > 1))
        {
            Require(DirectX::GenerateMipMaps(*base, filter, 0, mips));
            images = mips.GetImages();
            count = mips.GetImageCount();
        }
        TextureLevel dimensions{static_cast<unsigned int>(base->width), static_cast<unsigned int>(base->height), {}};
        const bool blockCompressed = Compressed(settings, dimensions);
        if (blockCompressed)
        {
            DirectX::TexMetadata metadata{};
            metadata.width = base->width;
            metadata.height = base->height;
            metadata.depth = metadata.arraySize = 1;
            metadata.mipLevels = count;
            metadata.format = base->format;
            metadata.dimension = DirectX::TEX_DIMENSION_TEXTURE2D;
            // 色空間変換は縮小時に完了しているため、圧縮では格納値をそのまま符号化します。
            Require(DirectX::Compress(images, count, metadata, DXGI_FORMAT_BC3_UNORM, DirectX::TEX_COMPRESS_DEFAULT,
                                      DirectX::TEX_THRESHOLD_DEFAULT, compressed));
            images = compressed.GetImages();
        }
        std::vector<TextureLevel> result;
        result.reserve(count);
        for (size_t i = 0; i < count; ++i)
        {
            const auto& image = images[i];
            size_t rowPitch = 0, slicePitch = 0;
            Require(DirectX::ComputePitch(image.format, image.width, image.height, rowPitch, slicePitch));
            TextureLevel level{static_cast<unsigned int>(image.width), static_cast<unsigned int>(image.height),
                               std::vector<unsigned char>(slicePitch)};
            const auto rows = blockCompressed ? (std::max)(size_t(1), (image.height + 3) / 4) : image.height;
            for (size_t row = 0; row < rows; ++row)
            {
                std::memcpy(level.pixels.data() + row * rowPitch, image.pixels + row * image.rowPitch, rowPitch);
            }
            result.push_back(std::move(level));
        }
        return result;
    }
}
