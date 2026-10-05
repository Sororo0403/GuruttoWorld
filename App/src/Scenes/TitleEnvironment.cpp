#include "TitleEnvironment.h"
#include "SceneLayout.h"
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <Engine/Core/Log.h>
#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

namespace App
{
    bool TitleEnvironment::Initialize(Engine::DirectX12Renderer& renderer, const std::filesystem::path& root)
    {
        auto skyTexture = std::make_shared<Engine::Texture2D>();
        if (!skyTexture->Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), {}) ||
            !sky_.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), skyTexture, root / "Shaders/TitleSky.hlsl"))
            return false;
        if (!motes_.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), skyTexture,
            root / "Shaders/TitleMote.hlsl")) return false;
        if (!models_.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), root / "Shaders/TitleMesh.hlsl"))
            return false;
        if (!BuildStreet(root)) return false;
        camera_.SetPosition({ -0.8f, 1.8f, -7.0f });
        camera_.SetRotation(0.03f, 0.13f);
        light_.direction = { -0.5f, -0.8f, 0.6f };
        light_.color = { 1.0f, 0.95f, 0.84f };
        light_.ambientIntensity = 0.52f;
        light_.intensity = 0.76f;
        light_.specularStrength = 0.03f;
        return true;
    }

    void TitleEnvironment::Update(double deltaSeconds, bool enabled, bool active, bool settingsSelected, bool exitSelected)
    {
        motion_.Update(deltaSeconds, enabled, active, settingsSelected, exitSelected);
        camera_.SetPosition(motion_.CameraPosition());
        const auto rotation = motion_.CameraRotation();
        camera_.SetRotation(rotation[0], rotation[1]);
    }

    void TitleEnvironment::Draw(ID3D12GraphicsCommandList* commands, unsigned int width, unsigned int height)
    {
        if (width == 0 || height == 0) return;
        const float viewportWidth = static_cast<float>(width);
        const float viewportHeight = static_cast<float>(height);
        const float aspectRatio = viewportWidth / viewportHeight;
        Engine::SpriteDrawParameters skyParameters;
        skyParameters.size = { viewportWidth, viewportHeight };
        // 縦長時はカメラと同じ倍率で空の基準位置を保ちます。
        const float verticalSpan = std::max(1.0f, (16.0f / 9.0f) / aspectRatio);
        skyParameters.uvRect = { 0.0f, 0.5f - verticalSpan * 0.5f, 1.0f, 0.5f + verticalSpan * 0.5f };
        sky_.Draw(commands, width, height, skyParameters);
        // 狭いウィンドウでも16:9時の横方向の構図を保ちます。
        const float verticalFov = 2.0f * std::atan(std::tan(DirectX::XM_PIDIV4 * 0.5f) *
            std::max(1.0f, (16.0f / 9.0f) / aspectRatio));
        camera_.SetPerspective(verticalFov, aspectRatio, 0.1f, 220.0f);
        for (const auto& object : objects_) object.Draw(commands, camera_, light_);
        if (motion_.IsEnabled())
        {
            using namespace DirectX;
            auto billboard = XMMatrixInverse(nullptr, camera_.GetViewMatrix());
            billboard.r[3] = XMVectorSet(0, 0, 0, 1);
            const auto viewProjection = camera_.GetViewMatrix() * camera_.GetProjectionMatrix();
            for (unsigned int index = 0; index < 24; ++index)
            {
                const auto mote = motion_.Mote(index);
                XMFLOAT4X4 matrix;
                XMStoreFloat4x4(&matrix, XMMatrixScaling(0.075f, 0.075f, 0.075f) * billboard *
                    XMMatrixTranslation(mote[0], mote[1], mote[2]) * viewProjection);
                motes_.Draw(commands, matrix, { 1.0f, 0.92f, 0.65f, mote[3] });
            }
        }
    }

    bool TitleEnvironment::BuildStreet(const std::filesystem::path& root)
    {
        try
        {
            auto layout = SceneLayout::Load(root / "Assets/Scenes/TitleStreet.json");
            std::vector<Engine::Object3D> objects;
            objects.reserve(layout.objects.size());
            for (const auto& placement : layout.objects)
            {
                Engine::Object3D object;
                const auto model = models_.Load(root / placement.model);
                if (!model) throw std::runtime_error("Model could not be loaded: " + placement.id);
                object.SetModel(model);
                if (!object.SetTransform(placement.position, placement.rotation, placement.scale))
                    throw std::runtime_error("Invalid transform: " + placement.id);
                objects.push_back(std::move(object));
            }
            layout_ = std::move(layout);
            objects_ = std::move(objects);
            return true;
        }
        catch (const std::exception& error)
        {
            Engine::Log::Error(std::format("Title street could not be loaded: {}", error.what()));
            return false;
        }
    }
}
