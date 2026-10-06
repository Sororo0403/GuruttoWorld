#include "TransformGizmo.h"
#include "TransformMatrix.h"
#include "PanelLayout.h"
#include <ImGuizmo.h>
#include <algorithm>

namespace Editor
{
    void TransformGizmo::BeginFrame() { ImGuizmo::BeginFrame(); }

    void TransformGizmo::UpdateAndDraw(SceneRuntime::SceneWorld& world, const Engine::Camera& camera,
        EditState& state, const SceneViewport& viewport, bool active)
    {
        hovered_=false;
        if (state.SelectedIds().size()>1 && !dragging_) mode_=Mode::Move;
        const auto& objects=world.Layout().objects;
        const auto found=std::find_if(objects.begin(), objects.end(),
            [&](const auto& object) { return object.id==state.SelectedId(); });
        const auto& io=ImGui::GetIO();
        if (!active || found==objects.end() || !viewport.Valid() ||
            (!dragging_ && !viewport.Contains(io.MousePos.x, io.MousePos.y)) ||
            (dragging_ && draggingId_!=state.SelectedId()))
        {
            ImGuizmo::Enable(false);
            dragging_=false;
            return;
        }
        const auto current=*found;
        DirectX::XMFLOAT4X4 matrix;
        if (!world.WorldMatrix(current.id,matrix)) return;
        if (!Manipulate(current, camera, viewport, matrix)) return;
        ApplyTransform(world, state, current, matrix);
    }

    bool TransformGizmo::Manipulate(const SceneRuntime::ScenePlacement& current,
        const Engine::Camera& camera, const SceneViewport& viewport, DirectX::XMFLOAT4X4& matrix)
    {
        DirectX::XMFLOAT4X4 view, projection;
        DirectX::XMStoreFloat4x4(&view,camera.GetViewMatrix());
        DirectX::XMStoreFloat4x4(&projection,camera.GetProjectionMatrix());
        ImGuizmo::Enable(!ImGui::IsMouseDown(ImGuiMouseButton_Right));
        ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetRect(viewport.x,viewport.y,viewport.width,viewport.height);
        ImGuizmo::PushID(current.id.c_str());
        const auto operation=mode_==Mode::Move ? ImGuizmo::TRANSLATE : mode_==Mode::Rotate ? ImGuizmo::ROTATE : ImGuizmo::SCALE;
        const auto coordinateMode=local_ || mode_==Mode::Scale ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
        const float step=mode_==Mode::Move ? moveStep_ : mode_==Mode::Rotate ? angleStep_ : scaleStep_;
        const float snapValues[3]{step,step,step};
        const bool modified=ImGuizmo::Manipulate(&view._11,&projection._11,operation,coordinateMode,
            &matrix._11,nullptr,snap_ ? snapValues : nullptr);
        dragging_=ImGuizmo::IsUsing();
        hovered_=ImGuizmo::IsOver();
        ImGuizmo::PopID();
        if (dragging_) draggingId_=current.id;
        return modified;
    }
    void TransformGizmo::DrawToolbar(bool enabled, bool multiple)
    {
        ImGui::BeginDisabled(!enabled || dragging_);
        if (ImGui::RadioButton("Move", mode_==Mode::Move)) mode_=Mode::Move;
        ImGui::SameLine();
        ImGui::BeginDisabled(multiple);
        if (ImGui::RadioButton("Rotate", mode_==Mode::Rotate)) mode_=Mode::Rotate;
        ImGui::SameLine();
        if (ImGui::RadioButton("Scale", mode_==Mode::Scale)) mode_=Mode::Scale;
        ImGui::SameLine();
        ImGui::EndDisabled();
        ImGui::BeginDisabled(mode_==Mode::Scale);
        if (ImGui::Button(local_ || mode_==Mode::Scale ? "Local" : "World")) local_=!local_;
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Checkbox("Snap", &snap_);
        ImGui::SameLine();
        if (ImGui::Button("Snap settings")) ImGui::OpenPopup("Snap settings");
        if (ImGui::BeginPopup("Snap settings"))
        {
            ImGui::DragFloat("Move step", &moveStep_, 0.1f, 0.01f, 100, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::DragFloat("Angle step (deg)", &angleStep_, 1, 1, 180, "%.0f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::DragFloat("Scale step", &scaleStep_, 0.05f, 0.01f, 10, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::EndPopup();
        }
        ImGui::EndDisabled();
        if (invalidTransform_) { ImGui::SameLine(); ImGui::TextUnformatted("Invalid transform"); }
    }

    void TransformGizmo::ApplyTransform(SceneRuntime::SceneWorld& world, EditState& state,
        const SceneRuntime::ScenePlacement& current, const DirectX::XMFLOAT4X4& matrix)
    {
        if (mode_==Mode::Move)
        {
            DirectX::XMFLOAT4X4 previous;
            if (!world.WorldMatrix(current.id,previous)) { invalidTransform_=true; return; }
            const std::array<float,3> delta{matrix._41-previous._41,matrix._42-previous._42,matrix._43-previous._43};
            invalidTransform_=!state.TranslateSelection(world,delta);
            return;
        }
        auto transformed=current;
        invalidTransform_=!world.ToPlacement(current.id,matrix,transformed);
        if (invalidTransform_) return;
        if (mode_==Mode::Rotate) { transformed.position=current.position; transformed.scale=current.scale; }
        if (mode_==Mode::Scale) { transformed.position=current.position; transformed.rotation=current.rotation; }
        if (transformed.position==current.position && transformed.rotation==current.rotation && transformed.scale==current.scale) return;
        invalidTransform_=!state.SetTransform(world,current.id,transformed.position,transformed.rotation,transformed.scale);
        if (!invalidTransform_) state.ObjectChanged(current.id);
    }

}
