#pragma once
#include "AssetRevision.h"
#include <Engine/Animation/Skeleton.h>
#include <chrono>

namespace Editor
{
    class SkeletonPreview final
    {
    public:
        /// <summary>素材・カタログ世代が変わった骨格を再取得し、一時的な失敗は一定間隔で再試行します。</summary>
        std::shared_ptr<const Engine::SkeletonData> Load(
            const std::filesystem::path& path, size_t catalogRevision,
            std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());

    private:
        std::filesystem::path path_;
        AssetRevision source_{};
        size_t catalogRevision_ = 0;
        std::shared_ptr<const Engine::SkeletonData> rig_;
        std::chrono::steady_clock::time_point retryAt_{};
    };
}
