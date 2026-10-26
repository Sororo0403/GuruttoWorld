#pragma once
#include "SceneViewport.h"
#include "../Shell/PanelLayout.h"
#include <imgui.h>
#include <algorithm>
#include <cstdint>

namespace Editor
{
    class ScenePanel final
    {
    public:
        /// <summary>
        /// オーバーレイの構築中は Scene ウィンドウを現在のウィンドウに保ち、必ず End と対にして呼び出します。
        /// </summary>
        bool Begin(std::uint64_t texture, const char* name = "シーン###Scene");
        static void End()
        {
            ImGui::End();
        }
        const SceneViewport& Viewport() const
        {
            return viewport_;
        }
        bool Hovered() const
        {
            return hovered_;
        }
        bool Focused() const
        {
            return focused_;
        }
        /// <summary>要求された表示サイズを求めます。</summary>
        std::array<unsigned int, 2> RequestedSize() const;

    private:
        SceneViewport viewport_;
        bool hovered_ = false;
        bool focused_ = false;
    };
}
