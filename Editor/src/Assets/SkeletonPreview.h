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
            std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now())
        {
            try
            {
                const auto source = AssetRevision::Capture(path);
                if (path_ != path || source_ != source || catalogRevision_ != catalogRevision ||
                    (!rig_ && now >= retryAt_))
                {
                    std::string error;
                    rig_ = Engine::Skeleton::Load(path, error);
                    path_ = path;
                    source_ = source;
                    catalogRevision_ = catalogRevision;
                    retryAt_ = now + std::chrono::milliseconds(500);
                }
                return rig_;
            }
            catch (const std::exception&)
            {
                return {};
            }
        }

    private:
        std::filesystem::path path_;
        AssetRevision source_{};
        size_t catalogRevision_ = 0;
        std::shared_ptr<const Engine::SkeletonData> rig_;
        std::chrono::steady_clock::time_point retryAt_{};
    };
}
