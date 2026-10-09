#include <SceneRuntime/GenreGeometry.h>
#include <DirectXMath.h>
#include <Engine/Core/Json.h>
#include <cmath>
#include <stdexcept>
namespace {
void Quad(Engine::MeshData& mesh,const std::array<std::array<float,3>,4>& positions,const std::array<float,3>& normal,
    const std::array<float,4>& color,const std::array<float,4>& uv) {
    const auto start=static_cast<uint32_t>(mesh.vertices.size());
    const std::array<std::array<float,2>,4> coords{{{uv[0],uv[3]},{uv[0],uv[1]},{uv[2],uv[1]},{uv[2],uv[3]}}};
    for(size_t i=0;i<4;++i) {Engine::MeshVertex v; v.position=positions[i]; v.normal=normal; v.uv=coords[i]; v.color=color; mesh.vertices.push_back(v);}
    for(const auto offset:{0u,1u,2u,0u,2u,3u}) mesh.indices.push_back(start+offset);
}
}
namespace SceneRuntime {
Engine::MeshData GenreGeometry::Terrain(const TerrainComponent& terrain,const std::filesystem::path& root) {
    if(terrain.heights.size()!=static_cast<size_t>(terrain.columns)*terrain.rows||terrain.columns<2||terrain.rows<2||!std::isfinite(terrain.cellSize)||terrain.cellSize<=0) throw std::runtime_error("Terrain grid mismatch");
    Engine::MeshData mesh; if(!terrain.texture.empty()) mesh.texturePath=root/terrain.texture;
    const auto height=[&](unsigned int x,unsigned int z){return terrain.heights[z*terrain.columns+x];};
    for(unsigned int z=0;z<terrain.rows;++z) for(unsigned int x=0;x<terrain.columns;++x) {
        const auto left=x==0?x:x-1,right=std::min(x+1,terrain.columns-1),back=z==0?z:z-1,front=std::min(z+1,terrain.rows-1);
        const float dx=(height(right,z)-height(left,z))/(static_cast<float>(right-left)*terrain.cellSize);
        const float dz=(height(x,front)-height(x,back))/(static_cast<float>(front-back)*terrain.cellSize);
        const float length=std::sqrt(dx*dx+dz*dz+1);
        Engine::MeshVertex v; v.position={x*terrain.cellSize,height(x,z),z*terrain.cellSize}; v.normal={-dx/length,1/length,-dz/length};
        v.uv={x*terrain.cellSize*terrain.textureScale,z*terrain.cellSize*terrain.textureScale}; v.color=terrain.color; mesh.vertices.push_back(v);
    }
    for(unsigned int z=0;z+1<terrain.rows;++z) for(unsigned int x=0;x+1<terrain.columns;++x) {
        const auto a=z*terrain.columns+x,b=a+1,c=a+terrain.columns,d=c+1;
        for(const auto index:{a,c,b,b,c,d}) mesh.indices.push_back(index);
    }
    return mesh;
}
Engine::MeshData GenreGeometry::Tilemap(const TilemapComponent& tiles,const std::filesystem::path& root) {
    if(tiles.tiles.size()!=static_cast<size_t>(tiles.columns)*tiles.rows||tiles.atlasColumns==0||tiles.atlasRows==0||!std::isfinite(tiles.cellSize)||tiles.cellSize<=0) throw std::runtime_error("Tilemap grid mismatch");
    Engine::MeshData mesh; if(!tiles.texture.empty()) mesh.texturePath=root/tiles.texture;
    for(unsigned int y=0;y<tiles.rows;++y) for(unsigned int x=0;x<tiles.columns;++x) {
        const int tile=tiles.tiles[y*tiles.columns+x]; if(tile<0) continue;
        const float a=x*tiles.cellSize,b=y*tiles.cellSize,c=a+tiles.cellSize,d=b+tiles.cellSize;
        const auto column=static_cast<unsigned int>(tile)%tiles.atlasColumns,row=static_cast<unsigned int>(tile)/tiles.atlasColumns;
        const float u=static_cast<float>(column)/tiles.atlasColumns,v=static_cast<float>(row)/tiles.atlasRows;
        Quad(mesh,{{{a,b,0},{a,d,0},{c,d,0},{c,b,0}}},{0,0,-1},tiles.color,{u,v,u+1.0f/tiles.atlasColumns,v+1.0f/tiles.atlasRows});
    }
    return mesh;
}
Engine::MeshData GenreGeometry::TileCollision(const TilemapComponent& tiles) {
    if(tiles.tiles.size()!=static_cast<size_t>(tiles.columns)*tiles.rows||!std::isfinite(tiles.cellSize)||tiles.cellSize<=0) throw std::runtime_error("Tile collision grid mismatch");
    Engine::MeshData mesh;
    for(unsigned int y=0;y<tiles.rows;++y) for(unsigned int x=0;x<tiles.columns;++x) {
        if(tiles.tiles[y*tiles.columns+x]<0) continue;
        const float a=x*tiles.cellSize,b=y*tiles.cellSize,c=a+tiles.cellSize,d=b+tiles.cellSize,l=-.05f,r=.05f;
        const std::array<float,4> color{1,1,1,1},uv{0,0,1,1};
        Quad(mesh,{{{a,b,l},{a,d,l},{c,d,l},{c,b,l}}},{0,0,-1},color,uv);
        Quad(mesh,{{{a,b,r},{c,b,r},{c,d,r},{a,d,r}}},{0,0,1},color,uv);
        Quad(mesh,{{{a,d,l},{a,d,r},{c,d,r},{c,d,l}}},{0,1,0},color,uv);
        Quad(mesh,{{{a,b,l},{c,b,l},{c,b,r},{a,b,r}}},{0,-1,0},color,uv);
        Quad(mesh,{{{a,b,l},{a,b,r},{a,d,r},{a,d,l}}},{-1,0,0},color,uv);
        Quad(mesh,{{{c,b,l},{c,d,l},{c,d,r},{c,b,r}}},{1,0,0},color,uv);
    }
    return mesh;
}
std::string GenreGeometry::Signature(const ScenePlacement& object) {
    SceneLayout layout; ScenePlacement p; p.id="procedural"; p.terrain=object.terrain; p.tilemap=object.tilemap; layout.objects.push_back(std::move(p)); return layout.Serialize();
}
}
