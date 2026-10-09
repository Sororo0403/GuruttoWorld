#pragma once
#include <SceneRuntime/SceneLayout.h>
namespace SceneRuntime {
class Navigation final {
public:
    /// <summary>地形の傾斜とワールドColliderの障害物をナビゲーションセルへ焼き込みます。</summary>
    static bool Bake(const SceneLayout& layout,ScenePlacement& mesh,std::string& error);
    /// <summary>ワールド座標の始点・終点からセル面上の経路を求めます。</summary>
    static std::vector<std::array<float,3>> Path(const SceneLayout& layout,const std::string& mesh,
        const std::array<float,3>& start,const std::array<float,3>& destination);
    /// <summary>NavAgentを経路に沿って固定時間進め、親ローカル座標へ反映します。</summary>
    static bool Advance(SceneLayout& layout,double seconds,std::string& error);
    /// <summary>実行するナビゲーションAgentが存在するか取得します。</summary>
    static bool HasAgents(const SceneLayout& layout);
};
}
