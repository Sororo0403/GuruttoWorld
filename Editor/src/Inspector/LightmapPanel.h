#pragma once
#include "../Editing/EditState.h"
#include <SceneRuntime/Rendering/LightmapBaker.h>
#include <SceneRuntime/Rendering/MaterialAsset.h>
#include <imgui.h>

namespace Editor
{
    class LightmapPanel final
    {
    public:
        /// <summary>選択した静的メッシュへ照明をベイクし、Undo可能なMaterial割り当てを予約します。</summary>
        void Draw(const SceneRuntime::SceneWorld& world, EditState& state, const std::filesystem::path& root,
                  bool enabled);

    private:
        /// <summary>画像と独立したMaterialを保存し、既存資産を変更せずにシーン編集を要求します。</summary>
        void Bake(const SceneRuntime::SceneWorld& world, EditState& state, const std::filesystem::path& root,
                  const SceneRuntime::ScenePlacement& placement);
        int resolution_ = 128;
        UINT64 serial_ = 0;
        std::string error_, result_;
    };
}
