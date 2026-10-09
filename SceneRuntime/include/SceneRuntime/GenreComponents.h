#pragma once
#include <string>
#include <vector>
#include <array>
#include <filesystem>
namespace SceneRuntime {
struct TerrainComponent {
    std::string id="terrain"; bool enabled=true;
    unsigned int columns=17,rows=17; float cellSize=1;
    std::vector<float> heights=std::vector<float>(17*17,0);
    std::filesystem::path texture; std::array<float,4> color{.35f,.65f,.25f,1}; float textureScale=.1f;
    bool operator==(const TerrainComponent&) const=default;
};
struct TilemapComponent {
    std::string id="tilemap"; bool enabled=true;
    unsigned int columns=8,rows=8,atlasColumns=1,atlasRows=1; float cellSize=1;
    std::vector<int> tiles=std::vector<int>(8*8,-1);
    std::filesystem::path texture; std::array<float,4> color{1,1,1,1};
    bool operator==(const TilemapComponent&) const=default;
};
struct NavMeshComponent {
    std::string id="navMesh"; bool enabled=true;
    unsigned int columns=16,rows=16; float cellSize=1,maxSlope=45,agentRadius=.2f;
    std::vector<float> heights=std::vector<float>(16*16,0);
    std::vector<bool> walkable=std::vector<bool>(16*16,true);
    bool baked=false;
    bool operator==(const NavMeshComponent&) const=default;
};
struct NavAgentComponent {
    std::string id="navAgent"; bool enabled=true;
    std::string mesh; std::array<float,3> destination{};
    float speed=3,stoppingDistance=.1f; bool moving=true;
    /// <summary>複製・Prefab・追加シーンのNavMesh参照を置き換えます。</summary>
    template<class Map> void Remap(const Map& ids) {const auto found=ids.find(mesh);if(found!=ids.end()) mesh=found->second;}
    bool operator==(const NavAgentComponent&) const=default;
};
}
