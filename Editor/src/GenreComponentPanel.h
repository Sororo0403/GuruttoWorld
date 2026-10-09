#pragma once
#include "EditState.h"
#include "ProjectCatalog.h"
#include "EnvironmentPanel.h"
#include <SceneRuntime/Navigation.h>
#include <imgui.h>
#include <cmath>
namespace Editor::GenrePanel {
/// <summary>地形操作をUndoへまとめます。</summary>
inline void Track(EditState& state) {if(ImGui::IsItemActive()||ImGui::IsItemDeactivatedAfterEdit()) state.SetInteraction("component/"+std::to_string(ImGui::GetItemID()));}
/// <summary>グリッド解像度の変更で要素数を安全に更新します。</summary>
template<class T,class Value> inline void Grid(EditState& state,T& c,std::vector<Value>& values,int minimum,Value fallback) {
    int size[]{static_cast<int>(c.columns),static_cast<int>(c.rows)};
    if(ImGui::DragInt2("グリッドサイズ###Grid dimensions",size,1,minimum,128, "%d",ImGuiSliderFlags_AlwaysClamp)) {
        c.columns=static_cast<unsigned int>(size[0]);c.rows=static_cast<unsigned int>(size[1]);values.assign(static_cast<size_t>(c.columns)*c.rows,fallback);
    } Track(state);ImGui::DragFloat("セルサイズ###Cell size",&c.cellSize,.05f,.001f,1000,"%.3f",ImGuiSliderFlags_AlwaysClamp);Track(state);
}
/// <summary>描画テクスチャをアセット一覧から選択します。</summary>
inline void Texture(std::filesystem::path& path,const ProjectCatalog* catalog) {
    if(!catalog||!ImGui::BeginCombo("テクスチャ###Texture",ProjectCatalog::Text(path).c_str())) return;
    if(ImGui::Selectable("なし###None",path.empty())) path.clear();
    for(const auto& asset:catalog->Assets()) if(asset.kind==AssetKind::Texture&&ImGui::Selectable(ProjectCatalog::Text(asset.path).c_str(),asset.path==path)) path=asset.path;
    ImGui::EndCombo();
}
/// <summary>Terrainの高さブラシと描画設定を編集します。</summary>
inline void Terrain(EditState& state,SceneRuntime::ScenePlacement& p,const ProjectCatalog* catalog) {
    if(!p.terrain||!ImGui::CollapsingHeader("地形###Terrain",ImGuiTreeNodeFlags_DefaultOpen)) return;auto& c=*p.terrain;ImGui::PushID(c.id.c_str());
    ImGui::Checkbox("有効###Enabled",&c.enabled);Grid(state,c,c.heights,2,0.0f);Texture(c.texture,catalog);ImGui::ColorEdit4("色###Color",c.color.data());Track(state);
    ImGui::DragFloat("テクスチャ倍率###Texture scale",&c.textureScale,.01f,.00001f,1000,"%.3f",ImGuiSliderFlags_AlwaysClamp);Track(state);
    static int brushCell[2]{},radius=1;static float height=0;
    ImGui::DragInt2("ブラシのセル###Brush cell",brushCell,1,0,128,"%d",ImGuiSliderFlags_AlwaysClamp);ImGui::SliderInt("半径（セル）###Brush radius",&radius,0,16);
    ImGui::DragFloat("塗る高さ###Paint height",&height,.1f,-100000,100000,"%.2f",ImGuiSliderFlags_AlwaysClamp);
    if(ImGui::Button("高さを塗る###Paint height")) for(unsigned int z=0;z<c.rows;++z) for(unsigned int x=0;x<c.columns;++x)
        if(std::hypot(static_cast<float>(static_cast<int>(x)-brushCell[0]),static_cast<float>(static_cast<int>(z)-brushCell[1]))<=radius) c.heights[z*c.columns+x]=height;
    ImGui::SameLine();if(ImGui::Button("平坦化###Flatten")) std::fill(c.heights.begin(),c.heights.end(),height);
    if(!p.boxCollider&&ImGui::Button("地形Colliderを追加###Terrain collider")) {p.boxCollider.emplace();p.boxCollider->id=EnvironmentPanel::NewId(p,"collider");p.boxCollider->shape="terrain";}
    if(ImGui::Button("削除###Remove")) {p.terrain.reset();if(p.boxCollider&&p.boxCollider->shape=="terrain") p.boxCollider.reset();}ImGui::PopID();
}
/// <summary>Tilemapのセルをクリックで描き換えます。</summary>
inline void TileGrid(SceneRuntime::TilemapComponent& c,int brush) {
    if(ImGui::BeginChild("Tile grid",{0,180},true)) for(unsigned int y=0;y<std::min(c.rows,32u);++y) {
        for(unsigned int x=0;x<std::min(c.columns,32u);++x) {
            ImGui::PushID(static_cast<int>(y*c.columns+x));const auto tile=c.tiles[y*c.columns+x];
            if(ImGui::Button(tile<0?"-":std::to_string(tile).c_str(),{24,24})) c.tiles[y*c.columns+x]=brush;
            ImGui::PopID();if(x+1<std::min(c.columns,32u)) ImGui::SameLine(0,1);
        }
    }ImGui::EndChild();
}
/// <summary>Tilemapのアトラスとセル描画を編集します。</summary>
inline void Tiles(EditState& state,SceneRuntime::ScenePlacement& p,const ProjectCatalog* catalog) {
    if(!p.tilemap||!ImGui::CollapsingHeader("タイルマップ###Tilemap",ImGuiTreeNodeFlags_DefaultOpen)) return;auto& c=*p.tilemap;ImGui::PushID(c.id.c_str());
    ImGui::Checkbox("有効###Enabled",&c.enabled);Grid(state,c,c.tiles,1,-1);Texture(c.texture,catalog);ImGui::ColorEdit4("色###Color",c.color.data());Track(state);
    int atlas[]{static_cast<int>(c.atlasColumns),static_cast<int>(c.atlasRows)};
    if(ImGui::DragInt2("アトラス分割###Atlas dimensions",atlas,1,1,64,"%d",ImGuiSliderFlags_AlwaysClamp)) {c.atlasColumns=static_cast<unsigned int>(atlas[0]);c.atlasRows=static_cast<unsigned int>(atlas[1]);for(auto& tile:c.tiles) if(tile>=atlas[0]*atlas[1]) tile=-1;}Track(state);
    static int brush=0;ImGui::SliderInt("塗るタイル（-1で消去）###Paint tile",&brush,-1,atlas[0]*atlas[1]-1);brush=std::clamp(brush,-1,atlas[0]*atlas[1]-1);
    static int cell[2]{};ImGui::DragInt2("対象セル###Cell",cell,1,0,127,"%d",ImGuiSliderFlags_AlwaysClamp);
    if(ImGui::Button("セルに塗る###Paint cell")&&cell[0]<static_cast<int>(c.columns)&&cell[1]<static_cast<int>(c.rows)) c.tiles[static_cast<size_t>(cell[1])*c.columns+cell[0]]=brush;
    ImGui::SameLine();if(ImGui::Button("全面を塗る###Fill")) std::fill(c.tiles.begin(),c.tiles.end(),brush);
    TileGrid(c,brush);
    if(!p.boxCollider&&ImGui::Button("タイルColliderを追加###Tile collider")) {p.boxCollider.emplace();p.boxCollider->id=EnvironmentPanel::NewId(p,"collider");p.boxCollider->shape="tilemap";}
    if(ImGui::Button("削除###Remove")) {p.tilemap.reset();if(p.boxCollider&&p.boxCollider->shape=="tilemap") p.boxCollider.reset();}ImGui::PopID();
}
/// <summary>NavMeshの焼き込みと通行可能セルを編集します。</summary>
inline void NavMesh(EditState& state,SceneRuntime::ScenePlacement& p,const SceneRuntime::SceneLayout* scene) {
    if(!p.navMesh||!ImGui::CollapsingHeader("ナビゲーション面###NavMesh",ImGuiTreeNodeFlags_DefaultOpen)) return;auto& c=*p.navMesh;ImGui::PushID(c.id.c_str());
    ImGui::Checkbox("有効###Enabled",&c.enabled);const auto oldColumns=c.columns,oldRows=c.rows;Grid(state,c,c.heights,1,0.0f);
    if(oldColumns!=c.columns||oldRows!=c.rows) {c.walkable.assign(c.heights.size(),true);c.baked=false;}
    ImGui::SliderFloat("最大傾斜（度）###Max slope",&c.maxSlope,0,89);Track(state);ImGui::DragFloat("Agent半径###Agent radius",&c.agentRadius,.05f,0,100,"%.2f",ImGuiSliderFlags_AlwaysClamp);Track(state);
    static std::string error;
    if(ImGui::Button("地形・障害物からBake###Bake navigation")) {
        SceneRuntime::SceneLayout layout=scene?*scene:SceneRuntime::SceneLayout{};auto found=std::find_if(layout.objects.begin(),layout.objects.end(),[&](const auto& item){return item.id==p.id;});
        if(found==layout.objects.end()) layout.objects.push_back(p);else *found=p;SceneRuntime::Navigation::Bake(layout,p,error);
    }
    if(!error.empty()) ImGui::TextWrapped("%s",error.c_str());ImGui::Text("Bake: %s / %zu cells",c.baked?"ready":"required",c.walkable.size());
    static int cell[2]{};static bool walkable=true;ImGui::DragInt2("対象セル###Cell",cell,1,0,127,"%d",ImGuiSliderFlags_AlwaysClamp);ImGui::Checkbox("通行可能###Walkable",&walkable);
    if(ImGui::Button("セルを書き換え###Set cell")&&cell[0]<static_cast<int>(c.columns)&&cell[1]<static_cast<int>(c.rows)) {c.walkable[static_cast<size_t>(cell[1])*c.columns+cell[0]]=walkable;c.baked=true;}
    if(ImGui::Button("削除###Remove")) p.navMesh.reset();ImGui::PopID();
}
/// <summary>Agentの目的地と移動設定を編集します。</summary>
inline void Agent(EditState& state,SceneRuntime::ScenePlacement& p,const SceneRuntime::SceneLayout* scene) {
    if(!p.navAgent||!ImGui::CollapsingHeader("ナビゲーションAgent###NavAgent",ImGuiTreeNodeFlags_DefaultOpen)) return;auto& c=*p.navAgent;ImGui::PushID(c.id.c_str());
    ImGui::Checkbox("有効###Enabled",&c.enabled);if(ImGui::BeginCombo("NavMeshオブジェクト###Mesh",c.mesh.c_str())) {if(ImGui::Selectable("なし###None",c.mesh.empty())) c.mesh.clear();if(scene) for(const auto& mesh:scene->objects) if(mesh.navMesh&&ImGui::Selectable(mesh.name.empty()?mesh.id.c_str():mesh.name.c_str(),c.mesh==mesh.id)) c.mesh=mesh.id;ImGui::EndCombo();}
    ImGui::DragFloat3("目的地（ワールド）###Destination",c.destination.data(),.1f,-1000000,1000000,"%.2f",ImGuiSliderFlags_AlwaysClamp);Track(state);
    ImGui::DragFloat("速度###Speed",&c.speed,.1f,0,1000,"%.2f",ImGuiSliderFlags_AlwaysClamp);Track(state);ImGui::DragFloat("停止距離###Stopping distance",&c.stoppingDistance,.01f,0,1000,"%.2f",ImGuiSliderFlags_AlwaysClamp);Track(state);ImGui::Checkbox("移動する###Moving",&c.moving);
    if(ImGui::Button("削除###Remove")) p.navAgent.reset();ImGui::PopID();
}
/// <summary>ジャンル支援コンポーネントをInspectorへ描画します。</summary>
inline bool Draw(EditState& state,SceneRuntime::ScenePlacement& p,const ProjectCatalog* catalog,const SceneRuntime::SceneLayout* scene) {const auto before=p;Terrain(state,p,catalog);Tiles(state,p,catalog);NavMesh(state,p,scene);Agent(state,p,scene);return !p.SameComponents(before);}
/// <summary>ジャンル支援コンポーネント追加メニューを描画します。</summary>
inline bool Add(SceneRuntime::ScenePlacement& p) {
    bool edited=false;
    if(ImGui::MenuItem("地形###Terrain",nullptr,false,!p.terrain&&!p.tilemap&&!p.meshRenderer)) {p.terrain.emplace();p.terrain->id=EnvironmentPanel::NewId(p,"terrain");edited=true;}
    if(ImGui::MenuItem("タイルマップ###Tilemap",nullptr,false,!p.terrain&&!p.tilemap&&!p.meshRenderer)) {p.tilemap.emplace();p.tilemap->id=EnvironmentPanel::NewId(p,"tilemap");edited=true;}
    if(ImGui::MenuItem("ナビゲーション面###NavMesh",nullptr,false,!p.navMesh)) {p.navMesh.emplace();p.navMesh->id=EnvironmentPanel::NewId(p,"navMesh");edited=true;}
    if(ImGui::MenuItem("ナビゲーションAgent###NavAgent",nullptr,false,!p.navAgent)) {p.navAgent.emplace();p.navAgent->id=EnvironmentPanel::NewId(p,"navAgent");edited=true;}return edited;
}
}
