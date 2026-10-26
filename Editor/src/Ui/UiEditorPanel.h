#pragma once
#include "UiCanvasPanel.h"
#include "UiLayoutTools.h"
#include <imgui.h>

namespace Editor
{
    class UiEditorPanel final
    {
    public:
        bool Visible() const
        {
            return visible_;
        }
        const SceneRuntime::UiState& PreviewState() const
        {
            return preview_;
        }
        std::array<unsigned int, 2> Resolution() const
        {
            return resolution_;
        }
        /// <summary>編集画面を描画します。</summary>
        void Draw(std::uint64_t texture, const SceneRuntime::SceneLayout& layout, EditState& state, bool enabled);

    private:
        /// <summary>状態のプレビューの編集画面を描画します。</summary>
        void DrawStatePreview(const SceneRuntime::SceneLayout& layout);

        /// <summary>アニメーションの再生時間を求めます。</summary>
        static float AnimationDuration(const SceneRuntime::SceneLayout& layout);
        /// <summary>アニメーションのプレビューの編集画面を描画します。</summary>
        void DrawAnimationPreview(const SceneRuntime::SceneLayout& layout);

        /// <summary>整列の編集画面を描画します。</summary>
        void DrawArrangement(const SceneRuntime::SceneLayout& layout, EditState& state, bool enabled);

        /// <summary>アンカーの編集画面を描画します。</summary>
        void DrawAnchors(const SceneRuntime::SceneLayout& layout, EditState& state, bool enabled);

        /// <summary>ビューポートの編集画面を描画します。</summary>
        void DrawViewport(std::uint64_t texture, const SceneRuntime::SceneLayout& layout, EditState& state,
                          bool enabled);

        UiCanvasPanel canvas_;
        SceneRuntime::UiState preview_;
        std::map<std::string, float> overrides_;
        std::array<unsigned int, 2> resolution_{1280, 720};
        float zoom_ = 1, grid_ = 10;
        bool snap_ = false, visible_ = false, resetScroll_ = false;
        bool animationPreview_ = false, playing_ = false;
        float previewTime_ = 0;
    };
}
