#pragma once
#include <array>
#include <cmath>
#include <optional>

namespace Editor
{
    // Screen-space content rectangle; excludes any future Scene panel title/tab bar.
    struct SceneViewport
    {
        float x = 0, y = 0, width = 0, height = 0;
        bool Valid() const
        {
            return std::isfinite(x) && std::isfinite(y) && std::isfinite(width) && std::isfinite(height) &&
                width > 0 && height > 0 && std::isfinite(x + width) && std::isfinite(y + height) &&
                std::isfinite(width / height) && width / height > 0;
        }
        bool Contains(float screenX, float screenY) const
        {
            return Valid() && screenX >= x && screenY >= y && screenX < x + width && screenY < y + height;
        }
        float Aspect() const { return width / height; } // Call only for a valid rectangle.
        std::optional<std::array<float, 2>> ToNdc(float screenX, float screenY) const
        {
            if (!Contains(screenX, screenY)) return std::nullopt;
            return std::array<float, 2>{(screenX - x) / width * 2 - 1, 1 - (screenY - y) / height * 2};
        }
        std::array<float, 2> ToScreen(float ndcX, float ndcY) const
        {
            return {x + (ndcX + 1) * 0.5f * width, y + (1 - ndcY) * 0.5f * height};
        }
    };
}
