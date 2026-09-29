#pragma once

#if defined(_DEBUG) || defined(ENGINE_DEVELOPMENT)
#include <array>

namespace App
{
    class DebugPanel final
    {
    public:
        /// <summary>
        /// FPS と描画オブジェクトの回転・背景色を操作する Debug 専用パネルを構築します。
        /// </summary>
        /// <param name="rotationY">Y 軸の回転角度（ラジアン）。</param>
        /// <param name="speedDegrees">1 秒あたりの回転角度（度）。</param>
        /// <param name="rotating">自動回転する場合は true。</param>
        /// <param name="backgroundColor">背景色の RGBA 値。</param>
        static void Draw(double& rotationY, float& speedDegrees, bool& rotating,
            std::array<float, 4>& backgroundColor);
    };
}
#endif
