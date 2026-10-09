#pragma once
#include "EditState.h"
#include "SceneViewport.h"
#include <SceneRuntime/SceneUi.h>
namespace Editor {
class UiCanvasPanel final {
public:
    void Draw(const SceneRuntime::SceneWorld&,EditState&,const SceneViewport&,bool enabled);
    void DrawLayout(const SceneRuntime::SceneLayout&,EditState&,const SceneViewport&,bool enabled);
    void DrawScene(const SceneRuntime::SceneWorld&,const Engine::Camera&,EditState&,const SceneViewport&,bool enabled);
    bool ConsumesMouse() const { return consumed_ || IsDragging(); }
    bool IsDragging() const { return start_.has_value(); }
    void SetPreviewState(SceneRuntime::UiState state,float displayScale=1) { preview_=std::move(state); displayScale_=displayScale; }
    void SetSnap(bool enabled,float grid) { snap_=enabled; grid_=grid; }
private:
    void UpdateDrag(EditState&,const std::array<float,2>& mouse);
    void SelectAndBegin(const SceneRuntime::SceneLayout&,EditState&,const SceneViewport&);
    std::optional<SceneRuntime::ScenePlacement> start_;
    std::vector<SceneRuntime::ScenePlacement> dragSelection_;
    std::array<float,2> mouse_{};
    float scale_=1,rotation_=0;
    bool resize_=false;
    bool rotate_=false,pivot_=false,snap_=false;
    float grid_=10,displayScale_=1;
    std::array<float,2> handle_{1,1},center_{};
    SceneRuntime::UiState preview_;
    SceneRuntime::UiRect startRect_;
    const SceneRuntime::SceneWorld* sceneWorld_=nullptr;
    const Engine::Camera* sceneCamera_=nullptr;
    bool consumed_=false;
    std::optional<std::array<float,2>> Mouse(const SceneRuntime::ScenePlacement&,const SceneViewport&,const std::array<float,2>&) const;
    std::optional<std::array<float,2>> Point(const SceneRuntime::ScenePlacement&,const SceneRuntime::UiRect&,const SceneViewport&,float,float) const;
    SceneRuntime::UiRect Resolve(const SceneRuntime::SceneLayout&,const SceneRuntime::ScenePlacement&,const SceneViewport&) const;
};
}
