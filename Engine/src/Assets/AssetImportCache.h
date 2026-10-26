#pragma once
#include <Engine/Assets/ModelImportInputs.h>

namespace Engine
{
    struct AssetImportRequest final
    {
        std::filesystem::path source;
        ArtifactDescriptor descriptor;
        ArtifactSources sources;
        std::shared_ptr<ArtifactRequests> owner;
        ArtifactTicket ticket;
        /// <summary>現在の所有者と入力世代に一致するCPU成果物だけを受け付けます。</summary>
        std::shared_ptr<const ArtifactData> Publish(std::string& error) const;
        /// <summary>同じCPU成果物と移動可能な入力証明を、配布用の保存先へ出力します。</summary>
        bool Cook(const std::filesystem::path& directory, std::string& error) const;
    };
    /// <summary>種類とプロジェクトごとに非同期要求を共有し、同梱キャッシュも参照します。</summary>
    std::shared_ptr<AssetImportRequest> RequestAssetImport(
        const std::filesystem::path& path, const std::string& kind,
        std::function<std::string(const std::filesystem::path&)> build,
        std::function<void(const std::filesystem::path&, ArtifactDescriptor&, ArtifactSources&)> describe = {});
}
