#pragma once
#include <Engine/Graphics/Models/MeshData.h>
#include <memory>
#include <string>

namespace Engine
{
    struct AssetImportRequest;
    class ModelAssetRequest final
    {
    public:
        /// <summary>CPUメッシュの取り込みが完了したかを返します。失敗時も完了です。</summary>
        bool Ready() const;
        /// <summary>現在の要求世代と入力を再検証して、不変のCPUメッシュを取得します。</summary>
        std::shared_ptr<const std::vector<MeshData>> Publish(std::string& error) const;

    private:
        friend class ModelAssets;
        std::shared_ptr<AssetImportRequest> request_;
    };
    class ModelAssets final
    {
    public:
        /// <summary>共通成果物のキーでCPU取り込みを非同期に開始し、同時要求を共有します。</summary>
        static ModelAssetRequest Request(const std::filesystem::path& path);
        /// <summary>ローカル・同梱キャッシュも参照し、取得に成功した不変メッシュだけを返します。</summary>
        static std::shared_ptr<const std::vector<MeshData>> Load(const std::filesystem::path& path, std::string& error);
        /// <summary>同じCPU形式と入力証明を、配布用の保存先へ出力します。</summary>
        static bool Cook(const std::filesystem::path& path, const std::filesystem::path& directory, std::string& error);
    };
}
