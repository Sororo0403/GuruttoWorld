#pragma once
#include <SceneRuntime/SceneLayout.h>
#include <Engine/Graphics/Models/MeshData.h>
namespace SceneRuntime {
class GenreGeometry final {
public:
    /// <summary>高さマップを法線・UVを含む三角形へ展開します。</summary>
    static Engine::MeshData Terrain(const TerrainComponent& terrain,const std::filesystem::path& root={});
    /// <summary>アトラス付きTilemapをXY平面の四角形へ展開します。</summary>
    static Engine::MeshData Tilemap(const TilemapComponent& tiles,const std::filesystem::path& root={});
    /// <summary>Tilemapの非空セルを厚み付き衝突用立方体へ展開します。</summary>
    static Engine::MeshData TileCollision(const TilemapComponent& tiles);
    /// <summary>描画キャッシュの安定した内容キーを取得します。</summary>
    static std::string Signature(const ScenePlacement& object);
};
}
