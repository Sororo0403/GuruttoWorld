#pragma once
#include <SceneRuntime/SceneLayout.h>

namespace SceneRuntime
{
    class Prefab final
    {
    public:
        static bool ValidPath(const std::filesystem::path& path);
        static SceneLayout Extract(const SceneLayout& scene,const std::string& rootId);
        static std::string Instantiate(SceneLayout& scene,const SceneLayout& asset,
            const std::filesystem::path& path,const std::array<float,3>& position);
        // Merge source updates while retaining per-instance property changes. Throws before caller commits.
        static void Refresh(SceneLayout& scene,const std::filesystem::path& assetsRoot);
        static void Bind(SceneLayout& scene,const std::string& rootId,const std::filesystem::path& path);
        static void Unpack(SceneLayout& scene,const std::string& rootId);
    };
}
