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

    bool TitleEnvironment::AddStreetSigns(const std::filesystem::path& root)
    {
        const auto roads = root / "Assets/Models/Title/Roads";
        constexpr float SidewalkTop = 0.16f;
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
        const auto commercial = root / "Assets/Models/Title/Surface/Commercial";
        const auto roads = root / "Assets/Models/Title/Roads";
        const auto surfaceRoads = root / "Assets/Models/Title/Surface/Roads";
        // 1 タイルを4ワールド単位に統一。道路上面は Y=0.08。
        constexpr float TileSize = 4.0f;
        constexpr float RoadTop = 0.08f;
        if (!AddObject(surfaceRoads / "ground.obj", { 0.0f, -0.12f, 60.0f }, 0.0f, { 160.0f, 4.0f, 180.0f }))
            return false;
        for (int tile = -2; tile < 22; ++tile)
        {
            const float z = static_cast<float>(tile) * TileSize;
            if (!AddObject(surfaceRoads / "road-straight.obj", { 0.0f, 0.0f, z }, DirectX::XM_PIDIV2, { TileSize, TileSize, TileSize }))
                return false;
            constexpr std::array<float, 2> Sides{ -1.0f, 1.0f };
            if (!std::all_of(Sides.begin(), Sides.end(), [&](float side)
                { return AddObject(surfaceRoads / "sidewalk.obj", { side * 2.8f, RoadTop, z },
                    0.0f, { 1.6f, 4.0f, TileSize }); })) return false;
        }
        // 正面を通りへ向け、高さと間口の違う建物を左右交互に配置します。
        struct BuildingPlacement
        {
            const char* model;
            float x;
            float z;
        };
        constexpr BuildingPlacement Buildings[] = {
            // 左右の建物配置を通常の街並みに戻します。
            { "building-k.obj", 10.0f, 3.0f }, { "building-h.obj", -4.8f, 5.0f },
            // ゲートを奥へ移した区画は元の建物へ戻します。
            // 右側の中景は建物の列を開き、中央広場を見せます。
            { "building-k.obj", -4.8f, 12.0f },
            // 高架の両端に重なる右Z=19・左Z=20の建物は配置しません。
            // 建物の列を通常の配置へ戻します。
            { "building-e.obj", -4.8f, 27.0f },
            { "building-c.obj", 4.8f, 34.0f }, { "building-h.obj", -4.8f, 35.0f }
        };
        for (const auto& building : Buildings)
        {
            const float yaw = building.x > 0.0f ? DirectX::XM_PIDIV2 : -DirectX::XM_PIDIV2;
            if (!AddObject(commercial / building.model, { building.x, RoadTop, building.z }, yaw,
                { TileSize, TileSize, TileSize })) return false;
        }
        // 高架は広場の奥へ移し、主役の建物の前を横切らないようにします。
        for (int tile = -1; tile <= 1; ++tile)
        {
            if (!AddObject(roads / "road-bridge.obj", { static_cast<float>(tile) * TileSize, RoadTop, 42.0f },
                0.0f, { TileSize, 8.0f, TileSize })) return false;
        }
        if (!AddDistantBuildings(commercial)) return false;
        if (!AddStreetSigns(root)) return false;
        if (!AddCentralPlaza(root)) return false;
        if (!AddPlazaDetails(root)) return false;
        if (!AddStorefrontDetails(root)) return false;
        return true;
    }

    bool TitleEnvironment::AddCentralPlaza(const std::filesystem::path& root)
    {
        const auto surfaceRoads = root / "Assets/Models/Title/Surface/Roads";
        const auto roads = root / "Assets/Models/Title/Roads";
        const auto commercial = root / "Assets/Models/Title/Surface/Commercial";
        // 車道の右側に12×16の歩行者広場。舗装の目地で空間に密度を付けます。
        for (int column = 0; column < 3; ++column)
        {
            for (int row = 0; row < 4; ++row)
            {
                if (!AddObject(surfaceRoads / "sidewalk.obj",
                    { 4.8f + column * 4.0f, 0.08f, 6.0f + row * 4.0f }, 0.0f,
                    { 4.0f, 4.0f, 4.0f })) return false;
            }
        }
        // 既存CC0の低いタイルで、主役の建物へ上がる三段の階段と基壇を作ります。
        for (int step = 0; step < 3; ++step)
        {
            if (!AddObject(roads / "tile-low.obj", { 9.0f, 0.16f + step * 0.16f, 17.5f + step * 0.5f },
                0.0f, { 7.5f, 8.0f, 1.0f })) return false;
        }
        if (!AddObject(roads / "tile-low.obj", { 9.0f, 0.08f, 22.0f }, 0.0f,
            { 8.5f, 28.0f, 7.0f })) return false;
        // 間口を広く取った市庁舎風の一棟を広場の奥に置き、正面をタイトル視点へ向けます。
        if (!AddObject(commercial / "building-h.obj", { 9.0f, 0.64f, 22.0f }, 0.0f,
            { 8.0f, 7.0f, 6.0f })) return false;
        // 低層の棟で右側の輪郭をつなぎ、主役の入口と階段を塞がないようにします。
        return AddObject(commercial / "building-c.obj", { 15.0f, 0.08f, 24.0f }, -0.18f,
            { 4.0f, 4.0f, 4.0f });
    }

    bool TitleEnvironment::AddPlazaDetails(const std::filesystem::path& root)
    {
        const auto roads = root / "Assets/Models/Title/Roads";
        const auto commercial = root / "Assets/Models/Title/Commercial";
        constexpr float PavementTop = 0.16f;
        // 広場の外周と反対側の歩道に街灯を並べ、前景から奥へのリズムを作ります。
        constexpr std::array<std::array<float, 3>, 5> Lights{
            std::array<float, 3>{ 3.3f, PavementTop, 5.0f },
            { 3.3f, PavementTop, 13.0f }, { 14.0f, PavementTop, 7.0f },
            { 14.0f, PavementTop, 15.0f }, { -2.8f, PavementTop, 3.0f }
        };
        for (const auto& position : Lights)
        {
            if (!AddObject(roads / "light-square-double.obj", position, 0.0f,
                { 6.0f, 6.0f, 6.0f })) return false;
        }
        // テーブル付きパラソルは広場の右端へ。中央の階段への動線は塞ぎません。
        for (float z : { 8.0f, 13.0f })
        {
            const auto model = z == 8.0f ? "detail-parasol-a.obj" : "detail-parasol-b.obj";
            if (!AddObject(commercial / model, { 12.0f, PavementTop, z }, z == 8.0f ? 0.2f : -0.3f,
                { 5.0f, 5.0f, 5.0f })) return false;
        }
        // 主役の入口に実際のCC0ひさしを付け、建物の用途と入口を読み取りやすくします。
        return AddObject(commercial / "detail-awning-wide.obj", { 9.0f, 0.64f, 19.8f }, 0.0f,
            { 8.0f, 8.0f, 8.0f });
    }

    bool TitleEnvironment::AddStorefrontDetails(const std::filesystem::path& root)
    {
        const auto commercial = root / "Assets/Models/Title/Commercial";
        struct Storefront
        {
            const char* model;
            std::array<float, 3> position;
            float yaw;
            float scale;
        };
        // 元モデルのひさしはローカル+Z側へ張り出します。建物正面と同じ向きで取り付けます。
        // 左の商店、右手前の店、広場奥のカフェに大小のひさしを使い分けます。
        const Storefront storefronts[] = {
            { "detail-awning-wide.obj", { -3.3f, 0.16f, 5.0f }, -DirectX::XM_PIDIV2, 4.5f },
            { "detail-awning.obj", { -3.4f, 0.16f, 12.0f }, -DirectX::XM_PIDIV2, 6.0f },
            { "detail-awning-wide.obj", { 8.5f, 0.16f, 3.0f }, DirectX::XM_PIDIV2, 5.0f },
            { "detail-awning-wide.obj", { 15.4f, 0.16f, 22.6f }, -0.18f, 5.0f }
        };
        for (const auto& storefront : storefronts)
        {
            if (!AddObject(commercial / storefront.model, storefront.position, storefront.yaw,
                { storefront.scale, storefront.scale, storefront.scale })) return false;
        }
        return true;
    }

    bool TitleEnvironment::AddDistantBuildings(const std::filesystem::path& commercial)
    {
        constexpr float RoadTop = 0.08f;
        // 遠景も同じ CC0 モデルで構成し、通りの奥に高層の目印を置きます。
        // 近景より広い間隔で配置して、空と建物の輪郭を見せます。
        constexpr std::array<int, 2> Sides{ -1, 1 };
        if (!std::all_of(Sides.begin(), Sides.end(), [&](int side)
        {
            const float direction = static_cast<float>(side);
            return AddObject(commercial / "building-e.obj", { direction * 12.0f, RoadTop, 54.0f },
                0.0f, { 5.0f, 5.0f, 5.0f }) &&
                AddObject(commercial / "building-k.obj", { direction * 24.0f, RoadTop, 68.0f },
                0.0f, { 5.0f, 6.0f, 5.0f }) &&
                AddObject(commercial / "building-skyscraper-a.obj", { direction * 20.0f, RoadTop, 98.0f },
                direction * 0.2f, { 5.0f, 6.0f, 5.0f }) &&
                AddObject(commercial / "building-skyscraper-a.obj", { direction * 40.0f, RoadTop, 116.0f },
                direction * 0.35f, { 6.0f, 8.0f, 6.0f });
        })) return false;
        if (!AddObject(commercial / "building-skyscraper-e.obj", { 2.0f, RoadTop, 94.0f },
            0.1f, { 5.0f, 10.0f, 5.0f })) return false;
        return true;
    }

}
