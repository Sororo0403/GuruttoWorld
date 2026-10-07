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
        // 既存資産は従来の照明を維持し、PBR は明示的に有効化します。
        bool physicallyBased=false;
        bool normalFlipY=false;
        UvTransform uv;
        // Empty uses the model's texture. Resource lifetime must cover all draws.
        std::shared_ptr<const Texture2D> texture;
        // 接線空間の法線画像（RGB、線形値）。未指定時は幾何法線を使います。
        std::shared_ptr<const Texture2D> normalTexture;
    };
}
