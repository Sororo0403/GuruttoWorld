#include "TitleUi.h"
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <algorithm>

namespace
{
    struct TitleUiFrame
    {
        const Engine::SpriteRenderer& atlas;
        ID3D12GraphicsCommandList* commands;
        unsigned int width;
        unsigned int height;
        const App::TitleMenu& menu;
        float scale;
        float left;
        float top;
        float offsetX = 0.0f;
        float opacity = 1.0f;
        static constexpr std::array<float, 4> White{ 1.0f, 1.0f, 1.0f, 1.0f };
        static constexpr std::array<float, 4> Ink{ 0.035f, 0.055f, 0.075f, 1.0f };
        static constexpr std::array<float, 4> Cream{ 1.0f, 0.973f, 0.882f, 1.0f };
        static constexpr std::array<float, 4> Pink{ 1.0f, 0.208f, 0.545f, 1.0f };
        static constexpr std::array<float, 4> Band{ 1024.0f, 0.0f, 1000.0f, 200.0f };
        TitleUiFrame(const Engine::SpriteRenderer& renderer, ID3D12GraphicsCommandList* commandList,
            unsigned int viewportWidth, unsigned int viewportHeight, const App::TitleMenu& titleMenu)
            : atlas(renderer), commands(commandList), width(viewportWidth), height(viewportHeight), menu(titleMenu),
              scale(std::min(static_cast<float>(width) / 1280.0f, static_cast<float>(height) / 720.0f)),
              left((static_cast<float>(width) - 1280.0f * scale) * 0.5f),
              top((static_cast<float>(height) - 720.0f * scale) * 0.5f) {}

        void SetIntroStage(float delay)
        {
            const float progress = std::clamp((menu.IntroProgress() - delay) / (1.0f - delay), 0.0f, 1.0f);
            const float eased = 1.0f - (1.0f - progress) * (1.0f - progress) * (1.0f - progress);
            offsetX = -180.0f * (1.0f - eased);
            opacity = eased;
        }
        void DrawPart(const std::array<float, 4>& source, const std::array<float, 4>& destination,
            const std::array<float, 4>& color, float rotation = 0.0f) const
        {
            Engine::SpriteDrawParameters part;
            part.uvRect = { source[0] / 2048.0f, source[1] / 2048.0f,
                (source[0] + source[2]) / 2048.0f, (source[1] + source[3]) / 2048.0f };
            part.position = { left + (destination[0] + offsetX) * scale, top + destination[1] * scale };
            part.size = { destination[2] * scale, destination[3] * scale };
            part.color = color;
            part.color[3] *= opacity;
            part.rotation = rotation;
            atlas.Draw(commands, width, height, part);
        }
        void DrawSettings()
        {
            const bool gamepad = menu.UsesGamepad();
            DrawPart(Band, { 100, 90, 1080, 540 }, Ink);
            DrawPart({ 0, 608, 800, 80 }, { 200, 120, 500, 50 }, Cream);
            for (int row = 0; row < 3; ++row)
            {
                const float y = 225.0f + row * 95.0f;
                const bool active = menu.GetSettingsRow() == row;
                offsetX = active ? 12.0f * menu.SelectionPulse() : 0.0f;
                DrawPart(Band, { 165, y - 8, 930, 80 }, active ? Pink : Ink);
                DrawPart({ 0, 1024.0f + row * 80, 800, 80 }, { 210, y, 440, 44 }, active ? Ink : White);
                if (row == 0)
                    DrawPart({ 1024, 1024.0f + menu.GetSettings().volume * 64, 400, 64 },
                    { 855, y + 3, 200, 32 }, active ? Ink : White);
                if (row == 1)
                    DrawPart({ menu.GetSettings().backgroundMotion ? 0.0f : 400.0f, 1264, 400, 64 },
                    { 855, y + 3, 200, 32 }, active ? Ink : White);
            }
            offsetX = 0.0f;
            DrawPart({ 0, 1872, 1920, 64 }, { 185, 510, 900, 30 }, Cream);
            if (menu.SaveFailed()) DrawPart({ 0, 1936, 1920, 64 }, { 185, 555, 900, 30 }, Pink);
            DrawPart(Band, { 100, 649, 1080, 46 }, Ink);
            DrawPart({ 0, gamepad ? 1808.0f : 1744.0f, 1920, 64 }, { 150, 655, 960, 32 }, White);
        }
        void DrawMenu()
        {
            const bool gamepad = menu.UsesGamepad();
            const auto selected = menu.GetSelected();
            SetIntroStage(0.0f);
            DrawPart({ 0, 0, 1024, 480 }, { 28, 36, 550, 258 }, White, -0.055f);
            DrawPart(Band, { 50, 302, 450, 48 }, Ink, -0.055f);
            DrawPart({ 0, 768, 940, 64 }, { 91, 308, 470, 32 }, White, -0.055f);
            constexpr std::array<float, 3> RowTop{ 399, 481, 563 };
            constexpr std::array<float, 3> LabelTop{ 512, 608, 688 };
            for (int row = 0; row < 3; ++row)
            {
                SetIntroStage(0.15f + row * 0.12f);
                const float y = RowTop[row];
                const bool active = row == static_cast<int>(selected);
                if (active)
                {
                    offsetX += 12.0f * menu.SelectionPulse();
                    DrawPart(Band, { 40, y, 402, 76 }, Cream, -0.035f);
                    DrawPart(Band, { 48, y + 8, 386, 60 }, Pink, -0.035f);
                    DrawPart({ 1024, 224, 128, 160 }, { 61, y + 20, 28, 35 }, Ink);
                }
                else DrawPart(Band, { 48, y + 8, row == 0 ? 300.0f : 220.0f, 58 }, Ink, -0.035f);
                const auto color = active ? Ink : White;
                const float labelHeight = row == 0 ? 96.0f : 80.0f;
                DrawPart({ 0, LabelTop[row], 800, labelHeight },
                { active ? 105.0f : 83.0f, y + 16, 400, labelHeight * 0.5f }, color);
            }
            SetIntroStage(0.5f);
            DrawPart(Band, { 34, 649, gamepad ? 730.0f : 500.0f, 46 }, Ink);
            DrawPart({ 0, gamepad ? 912.0f : 832.0f, gamepad ? 1400.0f : 940.0f, 64 },
            { 54, 655, gamepad ? 700.0f : 470.0f, 32 }, White);
        }
        void DrawStartPrompt()
        {
            SetIntroStage(0.0f);
            DrawPart({ 0, 0, 1024, 480 }, { 48, 54, 420, 197 }, White);
            SetIntroStage(0.2f);
            DrawPart(Band, { 390, 598, 500, 64 }, { 0.13f, 0.18f, 0.21f, 0.82f });
            DrawPart({ 0, 1600, 1000, 80 }, { 430, 615, 420, 34 }, Cream);
        }
    };
}

namespace App
{
    bool TitleUi::Initialize(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& root)
    {
        auto texture = std::make_shared<Engine::Texture2D>();
        auto white = std::make_shared<Engine::Texture2D>();
        if (!white->Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), {}) ||
            !cover_.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), white, root / "Shaders/Sprite.hlsl")) return false;
        return texture->Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), root / "Assets/Textures/Title/UiAtlas.png") &&
            atlas_.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), texture, root / "Shaders/Sprite.hlsl");
    }

    void TitleUi::Draw(ID3D12GraphicsCommandList* commands, unsigned int width, unsigned int height,
        const TitleMenu& menu) const
    {
        if (width == 0 || height == 0) return;
        const float viewportWidth = static_cast<float>(width);
        const float viewportHeight = static_cast<float>(height);
        TitleUiFrame frame{ atlas_, commands, width, height, menu };
        if (menu.UsesPressAnyButton()) frame.DrawStartPrompt();
        else if (menu.IsSettingsOpen())
        {
            frame.DrawSettings();
            return;
        }
        else frame.DrawMenu();
        const float transition = menu.TransitionProgress();
        if (transition > 0.0f)
        {
            Engine::SpriteDrawParameters cover;
            cover.position = { 0, 0 };
            cover.size = { viewportWidth * std::min(1.0f, transition * 1.25f), viewportHeight };
            cover.color = TitleUiFrame::Pink;
            cover_.Draw(commands, width, height, cover);
            cover.size[0] = viewportWidth * transition;
            cover.color = TitleUiFrame::Ink;
            cover_.Draw(commands, width, height, cover);
        }
    }
}
