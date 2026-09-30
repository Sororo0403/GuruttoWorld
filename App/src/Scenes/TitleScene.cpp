#include "TitleScene.h"
#include <Engine/Input/Keyboard.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <utility>

namespace App
{
    TitleScene::TitleScene(std::filesystem::path root) : root_(std::move(root)) {}

    bool TitleScene::Initialize(Engine::DirectX12Renderer& renderer)
    {
        if (!environment_.Initialize(renderer, root_)) return false;
        return ui_.Initialize(renderer, root_);
    }
    std::string TitleScene::Update(double, const Engine::Keyboard& keyboard)
    {
        return keyboard.IsPressed(DIK_RETURN) ? "Game" : "";
    }
    Engine::RenderResult TitleScene::Draw(Engine::DirectX12Renderer& renderer)
    {
        return renderer.Render({ 0.66f, 0.79f, 0.83f, 1.0f }, [&](ID3D12GraphicsCommandList* commands, float)
        {
            environment_.Draw(commands, renderer.GetWidth(), renderer.GetHeight());
            ui_.Draw(commands, renderer.GetWidth(), renderer.GetHeight());
        });
    }
}
