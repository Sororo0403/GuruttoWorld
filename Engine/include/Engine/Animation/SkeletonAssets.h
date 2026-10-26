#pragma once
#include <Engine/Animation/Skeleton.h>
#include <Engine/Assets/AssetArtifact.h>

namespace Engine
{
    struct AssetImportRequest;
    class SkeletonAssetRequest final
    {
    public:
        /// <summary>CPU取り込みの完了を返します。失敗時も完了として通知します。</summary>
        bool Ready() const;
        /// <summary>現在の要求世代と取り込み元を再検証し、不変の骨格成果物を取得します。</summary>
        std::shared_ptr<const SkeletonData> Publish(std::string& error) const;

    private:
        friend class SkeletonAssets;
        std::shared_ptr<AssetImportRequest> request_;
    };
    class SkeletonAssets final
    {
    public:
        /// <summary>骨格のCPU取り込みを非同期に開始し、同じ入力の要求を共有します。</summary>
        static SkeletonAssetRequest Request(const std::filesystem::path& path);
        /// <summary>同じCPU形式を、入力証明と共に配布用へ出力します。</summary>
        static bool Cook(const std::filesystem::path& path, const std::filesystem::path& directory, std::string& error);
        /// <summary>パスの移動後も、同じ不変なCPU成果物かを判定します。独自ローダーの値はポインターだけを比較します。</summary>
        static bool SameArtifact(const std::shared_ptr<const SkeletonData>& first,
                                 const std::shared_ptr<const SkeletonData>& second);
        /// <summary>共通成果物の完了を待ち、現世代と一致した骨格だけを取得します。</summary>
        static std::shared_ptr<const SkeletonData> Load(const std::filesystem::path& path, std::string& error);
    };
}
