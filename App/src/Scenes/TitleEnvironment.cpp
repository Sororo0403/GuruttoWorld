#include "TitleEnvironment.h"
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
        if (!models_.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), root / "Shaders/Mesh.hlsl"))
            return false;
        const auto commercial = root / "Assets/Models/Title/Commercial";
        const auto roads = root / "Assets/Models/Title/Roads";
        // 1 タイルを4ワールド単位に統一。道路上面は Y=0.08。
        constexpr float TileSize = 4.0f;
        constexpr float RoadTop = 0.08f;
        if (!AddObject(roads / "tile-low.obj", { 0.0f, -0.12f, 60.0f }, 0.0f, { 160.0f, 4.0f, 180.0f }))
            return false;
        for (int tile = -2; tile < 22; ++tile)
        {
            const float z = static_cast<float>(tile) * TileSize;
            if (!AddObject(roads / "road-straight.obj", { 0.0f, 0.0f, z }, DirectX::XM_PIDIV2, { TileSize, TileSize, TileSize }))
                return false;
            for (float side : { -1.0f, 1.0f })
            {
                if (!AddObject(roads / "tile-low.obj", { side * 2.8f, RoadTop, z }, 0.0f, { 1.6f, 4.0f, TileSize }))
                    return false;
            }
        }
        // 正面を通りへ向け、高さと間口の違う建物を左右交互に配置します。
        struct BuildingPlacement
        {
            const char* model;
            float x;
            float z;
        };
        constexpr BuildingPlacement Buildings[] = {
            { "building-k.obj", 4.8f, 4.0f }, { "building-h.obj", -4.8f, 5.0f },
            { "building-e.obj", 4.8f, 12.0f }, { "building-k.obj", -4.8f, 12.0f },
            { "building-h.obj", 4.8f, 19.0f }, { "building-c.obj", -4.8f, 20.0f },
            { "building-k.obj", 4.8f, 26.0f }, { "building-e.obj", -4.8f, 27.0f },
            { "building-c.obj", 4.8f, 34.0f }, { "building-h.obj", -4.8f, 35.0f }
        };
        for (const auto& building : Buildings)
        {
            const float yaw = building.x > 0.0f ? DirectX::XM_PIDIV2 : -DirectX::XM_PIDIV2;
            if (!AddObject(commercial / building.model, { building.x, RoadTop, building.z }, yaw,
                { TileSize, TileSize, TileSize })) return false;
        }
        // 奥の橋を横方向へ3枚接続。脚を地面に接地させ、橋面は約4単位の高さに置きます。
        for (int tile = -1; tile <= 1; ++tile)
        {
            if (!AddObject(roads / "road-bridge.obj", { static_cast<float>(tile) * TileSize, RoadTop, 18.0f },
                0.0f, { TileSize, 8.0f, TileSize })) return false;
        }
        // 遠景も同じ CC0 モデルで構成し、通りの奥に高層の目印を置きます。
        // 近景より広い間隔で配置して、空と建物の輪郭を見せます。
        for (int side : { -1, 1 })
        {
            const float direction = static_cast<float>(side);
            if (!AddObject(commercial / "building-e.obj", { direction * 12.0f, RoadTop, 54.0f },
                0.0f, { 5.0f, 5.0f, 5.0f }) ||
                !AddObject(commercial / "building-k.obj", { direction * 24.0f, RoadTop, 68.0f },
                0.0f, { 5.0f, 6.0f, 5.0f }) ||
                !AddObject(commercial / "building-skyscraper-a.obj", { direction * 20.0f, RoadTop, 98.0f },
                direction * 0.2f, { 5.0f, 6.0f, 5.0f }) ||
                !AddObject(commercial / "building-skyscraper-a.obj", { direction * 40.0f, RoadTop, 116.0f },
                direction * 0.35f, { 6.0f, 8.0f, 6.0f })) return false;
        }
        if (!AddObject(commercial / "building-skyscraper-e.obj", { 2.0f, RoadTop, 94.0f },
            0.1f, { 5.0f, 10.0f, 5.0f })) return false;
        camera_.SetPosition({ -0.8f, 1.8f, -7.0f });
        camera_.SetRotation(0.03f, 0.13f);
        light_.direction = { -0.5f, -0.8f, 0.6f };
        light_.ambientIntensity = 0.55f;
        light_.intensity = 0.65f;
        light_.specularStrength = 0.05f;
        return true;
    }

    bool TitleEnvironment::AddObject(const std::filesystem::path& path, const std::array<float, 3>& position,
        float yaw, const std::array<float, 3>& scale)
    {
        const auto model = models_.Load(path);
        if (!model)
        {
            Engine::Log::Error(std::format("Title environment model could not be loaded: {}", path.filename().string()));
            return false;
        }
        Engine::Object3D object;
        object.SetModel(model);
        if (!object.SetTransform(position, { 0.0f, yaw, 0.0f }, scale)) return false;
        objects_.push_back(std::move(object));
        return true;
    }

    void TitleEnvironment::Draw(ID3D12GraphicsCommandList* commands, float aspectRatio)
    {
        // 狭いウィンドウでも16:9時の横方向の構図を保ちます。
        const float verticalFov = 2.0f * std::atan(std::tan(DirectX::XM_PIDIV4 * 0.5f) *
            std::max(1.0f, (16.0f / 9.0f) / aspectRatio));
        camera_.SetPerspective(verticalFov, aspectRatio, 0.1f, 220.0f);
        for (const auto& object : objects_) object.Draw(commands, camera_, light_);
    }
}
