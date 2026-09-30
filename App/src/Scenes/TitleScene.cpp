#include "TitleScene.h"
#include <Engine/Input/Keyboard.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <algorithm>
#include <utility>

namespace App
{
    TitleScene::TitleScene(std::filesystem::path root) : root_(std::move(root)) {}

    bool TitleScene::Initialize(Engine::DirectX12Renderer& renderer)
    {
        if (!environment_.Initialize(renderer, root_)) return false;
        auto texture = std::make_shared<Engine::Texture2D>();
        return texture->Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), root_ / "Assets/Textures/Title.png") &&
            title_.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), texture, root_ / "Shaders/Sprite.hlsl");
    }
    std::string TitleScene::Update(double, const Engine::Keyboard& keyboard)
    {
        return keyboard.IsPressed(DIK_RETURN) ? "Game" : "";
    }
    Engine::RenderResult TitleScene::Draw(Engine::DirectX12Renderer& renderer)
    {
        return renderer.Render({ 0.38f, 0.68f, 0.86f, 1.0f }, [&](ID3D12GraphicsCommandList* commands, float aspectRatio)
        {
            environment_.Draw(commands, aspectRatio);
            const float width = static_cast<float>(renderer.GetWidth());
            const float height = static_cast<float>(renderer.GetHeight());
            const float scale = std::min({ 1.0f, width * 0.46f / 960.0f, height * 0.8f / 320.0f });
            Engine::SpriteDrawParameters parameters;
            parameters.size = { 960.0f * scale, 320.0f * scale };
            parameters.position = { width * 0.035f, (height - parameters.size[1]) * 0.5f };
            title_.Draw(commands, renderer.GetWidth(), renderer.GetHeight(), parameters);
        });
    }
}
