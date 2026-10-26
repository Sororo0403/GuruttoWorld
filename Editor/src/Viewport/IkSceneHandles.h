#pragma once
#include "../Editing/EditState.h"
#include "SceneViewport.h"
#include "IkHandleTransform.h"
#include <Engine/Graphics/Camera.h>
#include <imgui.h>
#include <ImGuizmo.h>

namespace Editor
{
    class IkSceneHandles final
    {
    public:
        /// <summary>処理に必要な資源を利用できるか確認します。</summary>
        bool Available(const SceneRuntime::SceneLayout& layout, const EditState& state) const;
        bool Enabled() const
        {
            return enabled_;
        }
        bool IsDragging() const
        {
            return dragging_;
        }
        bool ConsumesMouse() const
        {
            return dragging_ || hovered_;
        }
        void Cancel()
        {
            dragging_ = false;
            hovered_ = false;
        }
        /// <summary>操作用の項目を表示します。</summary>
        void Controls(const SceneRuntime::SceneLayout& layout, const EditState& state, const SceneViewport& viewport,
                      bool enabled);
        /// <summary>編集画面を描画します。</summary>
        void Draw(SceneRuntime::SceneWorld& world, const Engine::Camera& camera, EditState& state,
                  const SceneViewport& viewport, bool active);

    private:
        bool enabled_ = false, hint_ = false, dragging_ = false, hovered_ = false;
        size_t index_ = 0;
        std::string selected_;
    };
}
