#pragma once
#include "EditState.h"
#include "SceneViewport.h"
#include <Engine/Graphics/Camera.h>

namespace Editor
{
    class TransformGizmo final
    {
    public:
        static void BeginFrame();
        void UpdateAndDraw(SceneRuntime::SceneWorld& world, const Engine::Camera& camera,
            EditState& state, const SceneViewport& viewport, bool active);
        void DrawToolbar(bool enabled);
        bool IsDragging() const { return dragging_; }
        void Cancel() {dragging_=false;hovered_=false;}
        bool ConsumesMouse() const { return dragging_ || hovered_; }
        enum class Mode { Move, Rotate, Scale };
        void SetMode(Mode mode) { if (!dragging_) mode_=mode; }
    private:
        bool Manipulate(const SceneRuntime::ScenePlacement& current,
            const Engine::Camera& camera, const SceneViewport& viewport, DirectX::XMFLOAT4X4& matrix);
        void ApplyTransform(SceneRuntime::SceneWorld& world, EditState& state,
            const SceneRuntime::ScenePlacement& current, const DirectX::XMFLOAT4X4& matrix);
        void ApplyMultipleTransform(SceneRuntime::SceneWorld& world, EditState& state,
            const SceneRuntime::ScenePlacement& current, const DirectX::XMFLOAT4X4& matrix);
        Mode mode_ = Mode::Move;
        bool local_ = false, snap_ = false;
        float moveStep_ = 4.0f, angleStep_ = 15.0f, scaleStep_ = 0.25f;
        bool dragging_ = false, hovered_ = false, invalidTransform_ = false;
        std::string draggingId_;
    };
}
