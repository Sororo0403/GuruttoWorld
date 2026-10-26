#pragma once
#include <string>
#include <vector>

namespace Engine
{
    struct TextureImportSettings
    {
        bool mipmaps = false, srgb = false;
        unsigned int maxSize = 16384;
        std::string compression = "none";
        /// <summary>対応する画像サイズと圧縮形式を検証します。</summary>
        void Validate() const;
    };
    struct TextureLevel
    {
        unsigned int width = 0, height = 0;
        std::vector<unsigned char> pixels;
    };
    class TextureImport final
    {
    public:
        /// <summary>既存のアップロード形式に合わせ、基底サイズがBC3のブロック境界に一致するか確認します。</summary>
        static bool Compressed(const TextureImportSettings& settings, const TextureLevel& source);
        /// <summary>DirectXTexで画像縮小、色空間を考慮したミップ生成、BC3圧縮を行います。</summary>
        static std::vector<TextureLevel> Build(TextureLevel source, const TextureImportSettings& settings);
    };
}
