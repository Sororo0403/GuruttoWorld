#pragma once
#include "ObjectPanel.h"
#include <Engine/Graphics/Camera.h>

namespace Editor
{
    class TransformGizmo final
    {
    public:
        static void BeginFrame();
        void UpdateAndDraw(SceneRuntime::SceneWorld& world, const Engine::Camera& camera,
            ObjectPanel& panel, bool active);
        bool IsDragging() const { return dragging_; }
        bool ConsumesMouse() const { return dragging_ || hovered_; }
        enum class Mode { Move, Rotate, Scale };
        void SetMode(Mode mode) { if (!dragging_) mode_=mode; }
    private:
        Mode mode_ = Mode::Move;
        bool local_ = false, snap_ = false;
        float moveStep_ = 4.0f, angleStep_ = 15.0f, scaleStep_ = 0.25f;
        bool dragging_ = false, hovered_ = false, invalidTransform_ = false;
        std::string draggingId_;
    };
}
