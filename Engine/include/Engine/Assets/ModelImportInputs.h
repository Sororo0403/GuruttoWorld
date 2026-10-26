#pragma once
#include <Engine/Assets/AssetArtifact.h>

namespace Engine
{
    struct ModelImportInputs
    {
        ArtifactDescriptor descriptor;
        ArtifactSources sources;
        /// <summary>対応モデルの外部buffer・画像・MTLと各metaを、内容の版で固定します。</summary>
        static ModelImportInputs Capture(const std::filesystem::path& path, const std::string& kind);
    };
}
