#pragma once
#include <Engine/Assets/TextureImport.h>
#include <filesystem>
#include <memory>

namespace Engine
{
    struct AssetImportRequest;
    struct TextureAssetData
    {
        std::vector<TextureLevel> levels;
        bool compressed = false;
    };
    class TextureAssetRequest final
    {
    public:
        /// <summary>CPUの画像変換とミップ生成が完了したかを返します。</summary>
        bool Ready() const;
        /// <summary>現世代の入力に一致した不変のミップ群だけを取得します。</summary>
        std::shared_ptr<const TextureAssetData> Publish(std::string& error) const;

    private:
        friend class TextureAssets;
        std::shared_ptr<AssetImportRequest> request_;
    };
    class TextureAssets final
    {
    public:
        /// <summary>WIC画像変換とDirectXTex処理をGPUを使わず非同期に準備します。</summary>
        static TextureAssetRequest Request(const std::filesystem::path& path);
        /// <summary>同梱・ローカル成果物も参照し、現世代のCPUミップ群を取得します。</summary>
        static std::shared_ptr<const TextureAssetData> Load(const std::filesystem::path& path, std::string& error);
        /// <summary>既に取得したRGBA画素を、共有するCPU処理でミップ群に変換します。</summary>
        static std::shared_ptr<const TextureAssetData> PreparePixels(TextureLevel source,
                                                                     const TextureImportSettings& settings);
        /// <summary>画像と同じ取り込み形式・入力証明を配布用へ出力します。</summary>
        static bool Cook(const std::filesystem::path& path, const std::filesystem::path& directory, std::string& error);
    };
}
