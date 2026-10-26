#pragma once
#include <SceneRuntime/Ui/UiPacket.h>
#include <SceneRuntime/Ui/UiState.h>

namespace SceneRuntime
{
    struct UiRect
    {
        std::array<float, 2> position{}, size{};
        float rotation = 0, opacity = 1, scale = 1;
        bool visible = true;
        std::array<float, 4> clip{-100000, -100000, 100000, 100000};
        /// <summary>回転・透明度・切り抜きを反映したUI領域へ含まれるかを返します。</summary>
        bool Contains(float x, float y) const;
    };
    class UiGeometry final
    {
    public:
        /// <summary>描画用UI定義の既定状態を取得します。</summary>
        static UiState Defaults(const UiPacket& packet);
        /// <summary>指定viewの大きさと状態から、描画用UIの配置・切り抜き領域を解決します。</summary>
        static UiRect Resolve(const UiPacket& packet, const UiNode& node, unsigned int width, unsigned int height,
                              const UiState& state = {});
        /// <summary>入力欄の表示状態を描画用UI定義から取得します。</summary>
        static std::string InputText(const UiNode& node, const UiState& state);
    };
}
