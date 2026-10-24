#pragma once
#include <array>
#include <cmath>
#include <optional>

namespace Editor
{
    // Scene パネルの描画領域を画面座標で表します。タイトルバーやタブなどの操作領域は含めません。
    struct SceneViewport
    {
        float x = 0, y = 0, width = 0, height = 0;
        bool Valid() const
        {
            return std::isfinite(x) && std::isfinite(y) && std::isfinite(width) && std::isfinite(height) && width > 0 &&
                   height > 0 && std::isfinite(x + width) && std::isfinite(y + height) &&
                   std::isfinite(width / height) && width / height > 0;
        }
        bool Contains(float screenX, float screenY) const
        {
            return Valid() && screenX >= x && screenY >= y && screenX < x + width && screenY < y + height;
        }
        /// <summary>有効な矩形の幅と高さの比を返します。Valid が true の場合だけ呼び出してください。</summary>
        float Aspect() const
        {
            return width / height;
        }
        std::optional<std::array<float, 2>> ToNdc(float screenX, float screenY) const
        {
            if (!Contains(screenX, screenY))
                return std::nullopt;
            return std::array<float, 2>{(screenX - x) / width * 2 - 1, 1 - (screenY - y) / height * 2};
        }
        std::array<float, 2> ToScreen(float ndcX, float ndcY) const
        {
            return {x + (ndcX + 1) * 0.5f * width, y + (1 - ndcY) * 0.5f * height};
        }
    };
}
