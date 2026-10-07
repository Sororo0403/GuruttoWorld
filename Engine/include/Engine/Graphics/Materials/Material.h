#pragma once
#include <Engine/Graphics/Materials/UvTransform.h>
#include <Engine/Graphics/Resources/Texture2D.h>
#include <memory>

namespace Engine
{
    struct Material
    {
        std::array<float,4> color{1,1,1,1};
        float roughness=0.5f,metallic=0;
        bool transparent=false;
        UvTransform uv;
        // Empty uses the model's texture. Resource lifetime must cover all draws.
        std::shared_ptr<const Texture2D> texture;
    };
}
