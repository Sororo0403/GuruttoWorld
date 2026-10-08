#pragma once
#include <Engine/Graphics/Materials/UvTransform.h>
#include <Engine/Graphics/Resources/Texture2D.h>
#include <memory>
#include <vector>
#include <span>

namespace Engine
{
    enum class MaterialPass {All,Opaque,Transparent};
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
    using MaterialSlots=std::vector<std::shared_ptr<const Material>>;
    inline const Material* MaterialForSlot(size_t index,const Material* fallback,std::span<const std::shared_ptr<const Material>> slots) {
        return index<slots.size() && slots[index] ? slots[index].get():fallback;
    }
    inline bool MatchesMaterialPass(const Material* material,MaterialPass pass) {
        const bool transparent=material && (material->transparent || material->color[3]<1);
        return pass==MaterialPass::All || (pass==MaterialPass::Transparent)==transparent;
    }
}
