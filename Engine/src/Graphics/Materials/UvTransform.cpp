#include <Engine/Graphics/Materials/UvTransform.h>

#include <cmath>

namespace Engine
{
    std::array<float, 8> UvTransform::GetConstants() const
    {
        const float cosine = std::cos(rotation);
        const float sine = std::sin(rotation);
        return { scale[0] * cosine, -scale[1] * sine, translation[0], 0.0f,
            scale[0] * sine, scale[1] * cosine, translation[1], 0.0f };
    }
}
