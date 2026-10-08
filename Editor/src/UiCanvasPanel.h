#pragma once
#include "EditState.h"
#include "SceneViewport.h"
#include <SceneRuntime/SceneUi.h>
namespace Editor {
class UiCanvasPanel final {
public:
    void Draw(const SceneRuntime::SceneWorld&,EditState&,const SceneViewport&,bool enabled);
    void DrawLayout(const SceneRuntime::SceneLayout&,EditState&,const SceneViewport&,bool enabled);
    bool IsDragging() const { return start_.has_value(); }
private:
    void UpdateDrag(EditState&,const std::array<float,2>& mouse);
    void SelectAndBegin(const SceneRuntime::SceneLayout&,EditState&,const SceneViewport&);
    std::optional<SceneRuntime::ScenePlacement> start_;
    std::array<float,2> mouse_{};
    float scale_=1,rotation_=0;
    bool resize_=false;
};
}
