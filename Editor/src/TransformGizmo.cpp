#include "TransformGizmo.h"
#include "TransformMatrix.h"
#include "PanelLayout.h"
#include <ImGuizmo.h>
#include <algorithm>

namespace Editor
{
    void TransformGizmo::BeginFrame() { ImGuizmo::BeginFrame(); }

    void TransformGizmo::UpdateAndDraw(SceneRuntime::SceneWorld& world, const Engine::Camera& camera,
        ObjectPanel& panel, bool active)
    {
        DrawControls();
        hovered_=false;
        const auto& objects=world.Layout().objects;
        const auto found=std::find_if(objects.begin(), objects.end(),
            [&](const auto& object) { return object.id==panel.SelectedId(); });
        const auto& io=ImGui::GetIO();
        if (!active || found==objects.end() || io.DisplaySize.x<=0 || io.DisplaySize.y<=0 ||
            (dragging_ && draggingId_!=panel.SelectedId()))
        {
            ImGuizmo::Enable(false);
            dragging_=false;
            return;
        }
        const auto current=*found;
        auto matrix=TransformMatrix::Compose(current);
        if (!Manipulate(current, camera, matrix)) return;
        ApplyTransform(world, panel, current, matrix);
    }

    bool TransformGizmo::Manipulate(const SceneRuntime::ScenePlacement& current,
        const Engine::Camera& camera, DirectX::XMFLOAT4X4& matrix)
    {
        const auto& io = ImGui::GetIO();
        DirectX::XMFLOAT4X4 view, projection;
        DirectX::XMStoreFloat4x4(&view,camera.GetViewMatrix());
        DirectX::XMStoreFloat4x4(&projection,camera.GetProjectionMatrix());
        ImGuizmo::Enable(!ImGui::IsMouseDown(ImGuiMouseButton_Right));
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetRect(0,0,io.DisplaySize.x,io.DisplaySize.y);
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
    void TransformGizmo::DrawControls()
    {
        PanelLayout::Place(PanelLayout::Panel::Gizmo);
        if (ImGui::Begin("Transform Gizmo"))
        {
            ImGui::BeginDisabled(dragging_);
            if (ImGui::RadioButton("Move", mode_==Mode::Move)) mode_=Mode::Move;
            ImGui::SameLine();
            if (ImGui::RadioButton("Rotate", mode_==Mode::Rotate)) mode_=Mode::Rotate;
            ImGui::SameLine();
            if (ImGui::RadioButton("Scale", mode_==Mode::Scale)) mode_=Mode::Scale;
            if (mode_!=Mode::Scale)
            {
                if (ImGui::RadioButton("World", !local_)) local_=false;
                ImGui::SameLine();
                if (ImGui::RadioButton("Local", local_)) local_=true;
            }
            else ImGui::TextUnformatted("Scale uses local axes.");
            ImGui::Checkbox("Snap", &snap_);
            ImGui::DragFloat("Move step", &moveStep_, 0.1f, 0.01f, 100, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::DragFloat("Angle step (deg)", &angleStep_, 1, 1, 180, "%.0f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::DragFloat("Scale step", &scaleStep_, 0.05f, 0.01f, 10, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::EndDisabled();
            if (invalidTransform_) ImGui::TextWrapped("Cannot apply this transform. Previous placement is preserved.");
        }
        ImGui::End();
    }

    void TransformGizmo::ApplyTransform(SceneRuntime::SceneWorld& world, ObjectPanel& panel,
        const SceneRuntime::ScenePlacement& current, const DirectX::XMFLOAT4X4& matrix)
    {
        auto transformed=current;
        invalidTransform_=!TransformMatrix::Read(matrix,current,transformed);
        if (invalidTransform_) return;
        if (mode_==Mode::Move) { transformed.rotation=current.rotation; transformed.scale=current.scale; }
        if (mode_==Mode::Rotate) { transformed.position=current.position; transformed.scale=current.scale; }
        if (mode_==Mode::Scale) { transformed.position=current.position; transformed.rotation=current.rotation; }
        if (transformed.position==current.position && transformed.rotation==current.rotation && transformed.scale==current.scale) return;
        invalidTransform_=!world.SetTransform(current.id,transformed.position,transformed.rotation,transformed.scale);
        if (!invalidTransform_) panel.ObjectChanged(current.id);
    }

}
