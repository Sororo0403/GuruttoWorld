#pragma once
#include <Engine/Graphics/Renderers/SpriteRenderer.h>
#include "TitleMenu.h"

namespace Engine { class DirectX12Renderer; }

namespace App
{
    class TitleUi final
    {
    public:
        /// <summary>
        /// ロゴ・文字・帯・矢印をまとめた透明アトラスを読み込みます。
        /// </summary>
        bool Initialize(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& root);
        /// <summary>
        /// 1280×720 の基準配置を画面内に収め、各 UI 部品を独立して描画します。
        /// </summary>
        void Draw(ID3D12GraphicsCommandList* commands, unsigned int width, unsigned int height,
            const TitleMenu& menu) const;
    private:
        Engine::SpriteRenderer atlas_;
        Engine::SpriteRenderer cover_;
    };
}
