#include "TitleScene.h"
#include <Engine/Input/Keyboard.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <Engine/Core/Log.h>
#include <algorithm>
#include <utility>

namespace App
{
    TitleScene::TitleScene(std::filesystem::path root) : root_(std::move(root)) {}

    bool TitleScene::Initialize(Engine::DirectX12Renderer& renderer)
    {
        if (!modelManager_.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), root_ / "Shaders/Mesh.hlsl"))
            return false;
        const auto modelPath = root_ / "Assets/Models/Title/Commercial/building-c.obj";
        const auto model = modelManager_.Load(modelPath);
        if (!model)
        {
            Engine::Log::Error("Title building could not be loaded: Assets/Models/Title/Commercial/building-c.obj");
            return false;
        }
        building_.SetModel(model);
        // 素材の底面を基準に配置し、正面と側面を同時に確認できる角度にします。
        building_.SetTransform({ 1.0f, -0.75f, 0.0f }, { 0.0f, 0.55f, 0.0f }, { 2.0f, 2.0f, 2.0f });
        camera_.SetPosition({ 0.0f, 1.0f, -4.8f });
        camera_.SetRotation(0.0f, -0.12f);
        light_.direction = { -0.5f, -0.8f, 0.6f };
        light_.ambientIntensity = 0.55f;
        light_.intensity = 0.65f;
        light_.specularStrength = 0.05f;
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
        return renderer.Render({ 0.12f, 0.22f, 0.30f, 1.0f }, [&](ID3D12GraphicsCommandList* commands, float aspectRatio)
        {
            camera_.SetAspectRatio(aspectRatio);
            building_.Draw(commands, camera_, light_);
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
