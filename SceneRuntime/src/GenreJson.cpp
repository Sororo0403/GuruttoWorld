#include "GenreJson.h"
#include <cmath>
#include <stdexcept>
#include <algorithm>
namespace {
using Engine::Json;
float Number(const Json& o,const char* key,float low,float high) {
    const auto number=Engine::JsonNumber(o.at(key)); if(!std::isfinite(number)||number<low||number>high) throw std::runtime_error("Genre number out of range"); return static_cast<float>(number);
}
unsigned int Count(const Json& o,const char* key,unsigned int low,unsigned int high) {
    if(!o.at(key).is_number_integer()) throw std::runtime_error("Genre count must be integer");
    return static_cast<unsigned int>(Number(o,key,static_cast<float>(low),static_cast<float>(high)));
}
std::string String(const Json& o,const char* key) {
    auto value=o.at(key).get<std::string>(); if(value.size()>128||value.find('\0')!=std::string::npos) throw std::runtime_error("Invalid genre ID"); return value;
}
std::filesystem::path Texture(const Json& o) {
    const auto value=o.at("texture").get<std::string>(); const std::filesystem::path path(std::u8string(value.begin(),value.end()));
    if(value.size()>16384||value.find('\0')!=std::string::npos||(!value.empty()&&!value.starts_with("Assets/"))||path.is_absolute()||path.has_root_name()||
        std::any_of(path.begin(),path.end(),[](const auto& part){return part=="..";})) throw std::runtime_error("Invalid genre texture"); return path;
}
template<size_t N> std::array<float,N> Vector(const Json& o,const char* key,float low,float high) {
    const auto& values=Engine::JsonArray(o.at(key)); if(values.size()!=N) throw std::runtime_error("Genre vector size mismatch"); std::array<float,N> result{};
    for(size_t i=0;i<N;++i) {const auto value=Engine::JsonNumber(values[i]); if(!std::isfinite(value)||value<low||value>high) throw std::runtime_error("Genre vector range"); result[i]=static_cast<float>(value);} return result;
}
std::vector<float> Heights(const Json& o,size_t count) {
    const auto& values=Engine::JsonArray(o.at("heights")); if(values.size()!=count) throw std::runtime_error("Height grid size mismatch"); std::vector<float> result;
    for(const auto& value:values) {const auto number=Engine::JsonNumber(value); if(!std::isfinite(number)||std::abs(number)>100000) throw std::runtime_error("Height grid range"); result.push_back(static_cast<float>(number));} return result;
}
Json Base(const std::string& id,bool enabled,const char* type) {return {{"id",id},{"enabled",enabled},{"type",type}};}
void ReadTerrain(const Json& o,SceneRuntime::ScenePlacement& p) {
    if(p.terrain) throw std::runtime_error("Duplicate Terrain"); SceneRuntime::TerrainComponent c; c.id=String(o,"id"); c.enabled=o.at("enabled").get<bool>();
    c.columns=Count(o,"columns",2,129); c.rows=Count(o,"rows",2,129); c.cellSize=Number(o,"cellSize",.001f,1000); c.heights=Heights(o,static_cast<size_t>(c.columns)*c.rows);
    c.texture=Texture(o); c.color=Vector<4>(o,"color",0,1); c.textureScale=Number(o,"textureScale",.00001f,1000); p.terrain=std::move(c);
}
void ReadTiles(const Json& o,SceneRuntime::ScenePlacement& p) {
    if(p.tilemap) throw std::runtime_error("Duplicate Tilemap"); SceneRuntime::TilemapComponent c; c.id=String(o,"id"); c.enabled=o.at("enabled").get<bool>();
    c.columns=Count(o,"columns",1,128); c.rows=Count(o,"rows",1,128); c.atlasColumns=Count(o,"atlasColumns",1,64); c.atlasRows=Count(o,"atlasRows",1,64); c.cellSize=Number(o,"cellSize",.001f,1000);
    const auto& tiles=Engine::JsonArray(o.at("tiles")); if(tiles.size()!=static_cast<size_t>(c.columns)*c.rows) throw std::runtime_error("Tile grid size mismatch"); c.tiles.clear();
    for(const auto& tile:tiles) {if(!tile.is_number_integer()||Engine::JsonNumber(tile)<-1||Engine::JsonNumber(tile)>=c.atlasColumns*c.atlasRows) throw std::runtime_error("Tile atlas index invalid"); c.tiles.push_back(tile.get<int>());}
    c.texture=Texture(o); c.color=Vector<4>(o,"color",0,1); p.tilemap=std::move(c);
}
void ReadNavMesh(const Json& o,SceneRuntime::ScenePlacement& p) {
    if(p.navMesh) throw std::runtime_error("Duplicate NavMesh"); SceneRuntime::NavMeshComponent c; c.id=String(o,"id"); c.enabled=o.at("enabled").get<bool>();
    c.columns=Count(o,"columns",1,128); c.rows=Count(o,"rows",1,128); c.cellSize=Number(o,"cellSize",.001f,1000); c.maxSlope=Number(o,"maxSlope",0,89); c.agentRadius=Number(o,"agentRadius",0,100);
    c.heights=Heights(o,static_cast<size_t>(c.columns)*c.rows); const auto& walkable=Engine::JsonArray(o.at("walkable")); if(walkable.size()!=c.heights.size()) throw std::runtime_error("Navigation grid mismatch");
    c.walkable.clear(); for(const auto& value:walkable) c.walkable.push_back(value.get<bool>()); c.baked=o.at("baked").get<bool>(); p.navMesh=std::move(c);
}
void ReadAgent(const Json& o,SceneRuntime::ScenePlacement& p) {
    if(p.navAgent) throw std::runtime_error("Duplicate NavAgent"); SceneRuntime::NavAgentComponent c; c.id=String(o,"id"); c.enabled=o.at("enabled").get<bool>();
    c.mesh=String(o,"mesh"); c.destination=Vector<3>(o,"destination",-1000000,1000000); c.speed=Number(o,"speed",0,1000); c.stoppingDistance=Number(o,"stoppingDistance",0,1000); c.moving=o.at("moving").get<bool>(); p.navAgent=std::move(c);
}
}
namespace SceneRuntime {
bool ReadGenreComponent(const Json& o,ScenePlacement& p,const std::string& type) {
    if(type=="Terrain") ReadTerrain(o,p); else if(type=="Tilemap") ReadTiles(o,p); else if(type=="NavMesh") ReadNavMesh(o,p); else if(type=="NavAgent") ReadAgent(o,p); else return false; return true;
}
void WriteGenreComponents(Json& array,const ScenePlacement& p) {
    if(p.terrain) {const auto& c=*p.terrain; auto o=Base(c.id,c.enabled,"Terrain"); o["columns"]=c.columns;o["rows"]=c.rows;o["cellSize"]=c.cellSize;o["heights"]=c.heights;o["texture"]=c.texture.generic_string();o["color"]=c.color;o["textureScale"]=c.textureScale;array.push_back(std::move(o));}
    if(p.tilemap) {const auto& c=*p.tilemap;auto o=Base(c.id,c.enabled,"Tilemap");o["columns"]=c.columns;o["rows"]=c.rows;o["cellSize"]=c.cellSize;o["tiles"]=c.tiles;o["atlasColumns"]=c.atlasColumns;o["atlasRows"]=c.atlasRows;o["texture"]=c.texture.generic_string();o["color"]=c.color;array.push_back(std::move(o));}
    if(p.navMesh) {const auto& c=*p.navMesh;auto o=Base(c.id,c.enabled,"NavMesh");o["columns"]=c.columns;o["rows"]=c.rows;o["cellSize"]=c.cellSize;o["heights"]=c.heights;o["walkable"]=c.walkable;o["maxSlope"]=c.maxSlope;o["agentRadius"]=c.agentRadius;o["baked"]=c.baked;array.push_back(std::move(o));}
    if(p.navAgent) {const auto& c=*p.navAgent;auto o=Base(c.id,c.enabled,"NavAgent");o["mesh"]=c.mesh;o["destination"]=c.destination;o["speed"]=c.speed;o["stoppingDistance"]=c.stoppingDistance;o["moving"]=c.moving;array.push_back(std::move(o));}
}
}
