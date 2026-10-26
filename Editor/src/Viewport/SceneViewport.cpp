#include "SceneViewport.h"

namespace Editor
{
    auto SceneViewport::Valid() const -> bool
    {
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(width) && std::isfinite(height) && width > 0 &&
               height > 0 && std::isfinite(x + width) && std::isfinite(y + height) && std::isfinite(width / height) &&
               width / height > 0;
    }

    auto SceneViewport::ToNdc(float screenX, float screenY) const -> std::optional<std::array<float, 2>>
    {
        if (!Contains(screenX, screenY))
            return std::nullopt;
        return std::array<float, 2>{(screenX - x) / width * 2 - 1, 1 - (screenY - y) / height * 2};
    }
}
