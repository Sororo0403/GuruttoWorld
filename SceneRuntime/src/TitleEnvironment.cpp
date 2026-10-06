#include <SceneRuntime/TitleEnvironment.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <algorithm>
#include <cmath>
#include <utility>
#include <stdexcept>
#include <Engine/Core/Log.h>

namespace SceneRuntime
{
    bool TitleEnvironment::Initialize(Engine::DirectX12Renderer& renderer, const std::filesystem::path& root)
    {
        try
        {
            std::string error;
            return Initialize(renderer,root,SceneLayout::Load(root / "Assets/Scenes/TitleStreet.json"),error);
        }
        catch (const std::exception&) { return false; }
    }

    bool TitleEnvironment::Initialize(Engine::DirectX12Renderer& renderer, const std::filesystem::path& root,
        SceneLayout layout, std::string& error)
    {
        auto skyTexture = std::make_shared<Engine::Texture2D>();
        if (!skyTexture->Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), {}) ||
            !sky_.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), skyTexture, root / "Shaders/TitleSky.hlsl"))
        { error="Title sky could not be initialized"; return false; }
        if (!motes_.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), skyTexture,
            root / "Shaders/TitleMote.hlsl")) { error="Title motes could not be initialized"; return false; }
        if (!world_.Initialize(renderer,root,std::move(layout),root / "Shaders/TitleMesh.hlsl",&error)) return false;
        motion_=TitleAmbientMotion{};
        SceneRuntime::TitleView::SetHome(camera_);
        light_ = SceneRuntime::TitleView::Light();
        error.clear();
        return true;
    }

    void TitleEnvironment::Update(double deltaSeconds, bool enabled, bool active, bool settingsSelected, bool exitSelected)
    {
        if (active && std::isfinite(deltaSeconds) && deltaSeconds>0 && !world_.UpdateComponents(deltaSeconds))
            Engine::Log::Warning("Component update rejected an invalid inherited transform.");
        motion_.Update(deltaSeconds, enabled, active, settingsSelected, exitSelected);
        camera_.SetPosition(motion_.CameraPosition());
        const auto rotation = motion_.CameraRotation();
        camera_.SetRotation(rotation[0], rotation[1]);
    }

    void TitleEnvironment::Draw(ID3D12GraphicsCommandList* commands, unsigned int width, unsigned int height)
    {
        auto& camera = camera_;
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
        SceneRuntime::TitleView::SetProjection(camera, aspectRatio);
        world_.Draw(commands, camera, light_);
        if (motion_.IsEnabled())
        {
            using namespace DirectX;
            auto billboard = XMMatrixInverse(nullptr, camera.GetViewMatrix());
            billboard.r[3] = XMVectorSet(0, 0, 0, 1);
            const auto viewProjection = camera.GetViewMatrix() * camera.GetProjectionMatrix();
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

}
