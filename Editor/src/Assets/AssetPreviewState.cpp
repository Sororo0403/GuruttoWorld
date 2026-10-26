#include "AssetPreview.h"

namespace Editor
{
    void AssetPreview::Request(const ProjectAsset& asset)
    {
        if (selected_ != asset.path)
        {
            selected_ = asset.path;
            kind_ = asset.kind;
            requested_ = true;
        }
        visible_ = asset.kind == AssetKind::Model;
    }
}
