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
        auto skyTexture = std::make_shared<Engine::Texture2D>();
        if (!skyTexture->Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), {}) ||
            !sky_.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), skyTexture, root / "Shaders/TitleSky.hlsl"))
            return false;
        if (!motes_.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), skyTexture,
            root / "Shaders/TitleMote.hlsl")) return false;
        if (!models_.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), root / "Shaders/TitleMesh.hlsl"))
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
                // 右の歩道を切り、横道の入口を車止めのような段差で塞がないようにします。
                if (side > 0.0f && (tile == 1 || tile == 2)) continue;
                if (!AddObject(roads / "tile-low.obj", { side * 2.8f, RoadTop, z }, 0.0f, { 1.6f, 4.0f, TileSize }))
                    return false;
            }
        }
        // 横道は既存の無地タイルで舗装し、幹線の白線を路地へ引き込まない構成にします。
        for (int tile = 0; tile < 5; ++tile)
        {
            if (!AddObject(roads / "tile-low.obj", { 4.0f + tile * TileSize, 0.0f, 6.0f },
                0.0f, { TileSize, TileSize, TileSize })) return false;
        }
        for (float z : { 3.0f, 9.0f })
        {
            if (!AddObject(roads / "tile-low.obj", { 2.8f, RoadTop, z }, 0.0f,
                { 1.6f, TileSize, 2.0f })) return false;
        }
        // 右手前を低く短い建物に置き換え、次の建物との間に街角を作ります。
        struct BuildingPlacement
        {
            const char* model;
            float x;
            float z;
        };
        constexpr BuildingPlacement Buildings[] = {
            { "building-c.obj", 4.8f, 1.2f }, { "building-h.obj", -4.8f, 5.0f },
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
        // 横道の両側。大通りとは正面の向きを変えて、曲がった先の街区を示します。
        for (float x : { 11.0f, 16.0f, 21.0f })
        {
            if (!AddObject(commercial / "building-h.obj", { x, RoadTop, 1.8f }, 0.0f,
                { TileSize, TileSize, TileSize }) ||
                !AddObject(commercial / "building-e.obj", { x, RoadTop, 10.2f }, DirectX::XM_PI,
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
        // ロゴの右に輪郭が残る位置へ主塔を寄せ、段状の屋上で一般の高層と区別します。
        // 派生モデルの形状は既存CC0素材のまま。専用材質は他の建物へ波及させません。
        const auto landmark = root / "Assets/Models/Title/Landmark";
        constexpr float TowerX = 10.0f;
        constexpr float TowerZ = 76.0f;
        constexpr float TowerTop = RoadTop + 4.08f * 8.5f;
        if (!AddObject(landmark / "tower.obj", { TowerX, RoadTop, TowerZ }, 0.0f,
                { 7.5f, 8.5f, 7.5f }) ||
            !AddObject(landmark / "crown.obj", { TowerX, TowerTop, TowerZ }, 0.0f,
                { 12.8f, 35.0f, 11.8f }) ||
            !AddObject(landmark / "crown.obj", { TowerX, TowerTop + 0.7f, TowerZ }, 0.0f,
                { 10.0f, 25.0f, 9.0f }) ||
            !AddObject(landmark / "crown.obj", { TowerX, TowerTop + 1.2f, TowerZ }, 0.0f,
                { 0.65f, 180.0f, 0.65f })) return false;
        if (!AddGreeneryAndSigns(root)) return false;
        camera_.SetPosition(motion_.CameraPosition());
        camera_.SetRotation(0.06f, 0.08f);
        light_.direction = { -0.5f, -0.8f, 0.6f };
        light_.color = { 1.0f, 0.96f, 0.86f };
        light_.ambientIntensity = 0.48f;
        light_.intensity = 0.72f;
        light_.specularStrength = 0.03f;
        return true;
    }

    bool TitleEnvironment::AddGreeneryAndSigns(const std::filesystem::path& root)
    {
        const auto nature = root / "Assets/Models/Title/Nature";
        const auto roads = root / "Assets/Models/Title/Roads";
        constexpr float SidewalkTop = 0.16f;
        // 透過画像を使わず、草・花・葉の立体形状と材質色を描画します。
        for (int side : { -1, 1 })
        {
            const float direction = static_cast<float>(side);
            for (int patch = 0; patch < 16; ++patch)
            {
                const float index = static_cast<float>(patch);
                const float z = -1.5f + index * 2.8f + (side < 0 ? 0.7f : 0.0f);
                if (side > 0 && z >= 3.0f && z <= 10.0f) continue;
                const float yaw = index * 0.73f;
                const float grassScale = 1.5f + static_cast<float>(patch % 3) * 0.25f;
                if (!AddObject(nature / "grass_large.obj", { direction * 2.35f, SidewalkTop, z }, yaw,
                    { grassScale, grassScale, grassScale })) return false;
                if (patch % 2 == 0 && !AddObject(nature / "plant_bush.obj",
                    { direction * 3.0f, SidewalkTop, z + 0.5f }, yaw, { 3.0f, 2.8f, 2.6f })) return false;
                if (patch < 6 && !AddObject(nature / "flower_yellowA.obj",
                    { direction * 2.15f, SidewalkTop, z + 0.35f }, yaw, { 2.0f, 2.0f, 2.0f })) return false;
            }
            for (float z : { 9.0f, 23.0f, 40.0f })
            {
                if (side > 0 && z == 9.0f) continue;
                if (!AddObject(nature / "tree_small.obj", { direction * 7.0f, 0.08f, z },
                    direction * 0.4f, { 4.0f, 4.0f, 4.0f })) return false;
            }
        }
        // 実測した建物の高さに合わせて、近景の屋上に低木を置きます。
        for (int patch = 0; patch < 5; ++patch)
        {
            const float offset = static_cast<float>(patch) * 1.0f;
            if (!AddObject(nature / "plant_bush.obj", { -3.4f, 5.1f, 3.5f + offset * 0.6f }, offset,
                { 3.0f, 2.5f, 2.5f })) return false;
        }
        for (int patch = -4; patch <= 4; ++patch)
        {
            // 橋面の手前側の縁。脚や通路の中央を覆いません。
            if (!AddObject(nature / "plant_bush.obj", { static_cast<float>(patch) * 1.1f, 4.16f, 16.4f },
                static_cast<float>(patch) * 0.5f, { 2.8f, 1.8f, 2.0f })) return false;
        }
        return AddObject(roads / "road-sign-street.obj", { 2.85f, SidewalkTop, -0.5f }, -0.25f, { 4.0f, 4.0f, 4.0f }) &&
            AddObject(roads / "road-sign-empty.obj", { -2.85f, SidewalkTop, 10.0f }, 0.3f, { 4.0f, 4.0f, 4.0f });
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

    void TitleEnvironment::Update(double deltaSeconds, bool enabled, bool active)
    {
        motion_.Update(deltaSeconds, enabled, active);
        camera_.SetPosition(motion_.CameraPosition());
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
}
