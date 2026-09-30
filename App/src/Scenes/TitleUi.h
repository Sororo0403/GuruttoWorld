#pragma once
#include <Engine/Graphics/Renderers/SpriteRenderer.h>

namespace Engine { class DirectX12Renderer; }

namespace App
{
    class TitleUi final
    {
    public:
        /// <summary>
        /// ロゴ・文字・帯・矢印をまとめた透明アトラスを読み込みます。
        /// </summary>
        bool Initialize(Engine::DirectX12Renderer& renderer, const std::filesystem::path& root);
        /// <summary>
        /// 1280×720 の基準配置を画面内に収め、各 UI 部品を独立して描画します。
        /// </summary>
        void Draw(ID3D12GraphicsCommandList* commands, unsigned int width, unsigned int height) const;
    private:
        Engine::SpriteRenderer atlas_;
    };
}
