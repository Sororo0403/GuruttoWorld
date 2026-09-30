#include "TitleUi.h"
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <algorithm>

namespace App
{
    bool TitleUi::Initialize(Engine::DirectX12Renderer& renderer, const std::filesystem::path& root)
    {
        auto texture = std::make_shared<Engine::Texture2D>();
        return texture->Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), root / "Assets/Textures/Title/UiAtlas.png") &&
            atlas_.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), texture, root / "Shaders/Sprite.hlsl");
    }

    void TitleUi::Draw(ID3D12GraphicsCommandList* commands, unsigned int width, unsigned int height) const
    {
        if (width == 0 || height == 0) return;
        const float viewportWidth = static_cast<float>(width);
        const float viewportHeight = static_cast<float>(height);
        const float scale = std::min(viewportWidth / 1280.0f, viewportHeight / 720.0f);
        const float left = (viewportWidth - 1280.0f * scale) * 0.5f;
        const float top = (viewportHeight - 720.0f * scale) * 0.5f;
        const auto drawPart = [&](const std::array<float, 4>& source, const std::array<float, 4>& destination,
            const std::array<float, 4>& color, float rotation = 0.0f)
        {
            Engine::SpriteDrawParameters part;
            part.uvRect = { source[0] / 2048.0f, source[1] / 1024.0f,
                (source[0] + source[2]) / 2048.0f, (source[1] + source[3]) / 1024.0f };
            part.position = { left + destination[0] * scale, top + destination[1] * scale };
            part.size = { destination[2] * scale, destination[3] * scale };
            part.color = color;
            part.rotation = rotation;
            atlas_.Draw(commands, width, height, part);
        };
        constexpr std::array<float, 4> White{ 1.0f, 1.0f, 1.0f, 1.0f };
        constexpr std::array<float, 4> Ink{ 0.035f, 0.055f, 0.075f, 1.0f };
        constexpr std::array<float, 4> Cream{ 1.0f, 0.973f, 0.882f, 1.0f };
        constexpr std::array<float, 4> Pink{ 1.0f, 0.208f, 0.545f, 1.0f };
        constexpr std::array<float, 4> Band{ 1024.0f, 0.0f, 1000.0f, 200.0f };
        drawPart({ 0, 0, 1024, 480 }, { 28, 36, 550, 258 }, White, -0.055f);
        drawPart(Band, { 50, 302, 450, 48 }, Ink, -0.055f);
        drawPart({ 0, 768, 940, 64 }, { 91, 308, 470, 32 }, White, -0.055f);
        // 開始だけが操作可能な段階。設定・終了は次のコミットで接続します。
        drawPart(Band, { 40, 399, 402, 88 }, Cream, -0.035f);
        drawPart(Band, { 48, 407, 386, 72 }, Pink, -0.035f);
        drawPart({ 1024, 224, 128, 160 }, { 61, 420, 28, 35 }, Ink);
        drawPart({ 0, 512, 800, 96 }, { 105, 417, 400, 48 }, Ink);
        drawPart(Band, { 48, 504, 220, 58 }, Ink, -0.035f);
        drawPart({ 0, 608, 800, 80 }, { 83, 512, 400, 40 }, { 1, 1, 1, 0.45f });
        drawPart(Band, { 48, 574, 220, 58 }, Ink, -0.035f);
        drawPart({ 0, 688, 800, 80 }, { 83, 582, 400, 40 }, { 1, 1, 1, 0.45f });
        drawPart(Band, { 34, 649, 264, 46 }, Ink);
        drawPart({ 0, 832, 480, 64 }, { 54, 655, 240, 32 }, White);
    }
}
