#include "SkeletonPreview.h"

namespace Editor
{
    auto SkeletonPreview::Load(const std::filesystem::path& path, size_t catalogRevision,
                               std::chrono::steady_clock::time_point now) -> std::shared_ptr<const Engine::SkeletonData>
    {
        try
        {
            const auto source = AssetRevision::Capture(path);
            if (path_ != path || source_ != source || catalogRevision_ != catalogRevision || (!rig_ && now >= retryAt_))
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
}
