#include "TransformGizmo.h"
#include "GizmoTransform.h"
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
        if (!GizmoTransform::Build(world,current,mode_==Mode::Scale,matrix))
        { invalidTransform_=true; dragging_=false; ImGuizmo::Enable(false); return; }
        const bool modified=Manipulate(current,camera,viewport,matrix);
        if (dragging_) state.SetInteraction("gizmo/"+current.id+"/"+std::to_string(static_cast<int>(mode_)));
        if (!modified) return;
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
    void TransformGizmo::DrawToolbar(bool enabled)
    {
        ImGui::BeginDisabled(!enabled || dragging_);
        if (ImGui::RadioButton("移動###Move", mode_==Mode::Move)) mode_=Mode::Move;
        ImGui::SameLine();
        if (ImGui::RadioButton("回転###Rotate", mode_==Mode::Rotate)) mode_=Mode::Rotate;
        ImGui::SameLine();
        if (ImGui::RadioButton("拡縮###Scale", mode_==Mode::Scale)) mode_=Mode::Scale;
        ImGui::SameLine();
        ImGui::BeginDisabled(mode_==Mode::Scale);
        if (ImGui::Button(mode_==Mode::Scale ? "ローカル（拡縮）###Local (scale)" : local_ ? "ローカル###Local" : "ワールド###World")) local_=!local_;
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Checkbox("スナップ###Snap", &snap_);
        ImGui::SameLine();
        if (ImGui::Button("スナップ設定###Snap settings")) ImGui::OpenPopup("スナップ設定###Snap settings");
        if (ImGui::BeginPopup("スナップ設定###Snap settings"))
        {
            ImGui::DragFloat("移動間隔###Move step", &moveStep_, 0.1f, 0.01f, 100, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::DragFloat("角度間隔（度）###Angle step (deg)", &angleStep_, 1, 1, 180, "%.0f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::DragFloat("拡縮間隔###Scale step", &scaleStep_, 0.05f, 0.01f, 10, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::EndPopup();
        }
        ImGui::EndDisabled();
        if (invalidTransform_) { ImGui::SameLine(); ImGui::TextUnformatted("変換できません。不正な値やローカルのせん断は保存できません。"); }
    }

    void TransformGizmo::ApplyTransform(SceneRuntime::SceneWorld& world, EditState& state,
        const SceneRuntime::ScenePlacement& current, const DirectX::XMFLOAT4X4& matrix)
    {
        if (mode_==Mode::Move)
        {
            DirectX::XMFLOAT4X4 previous;
            if (!world.WorldMatrix(current.id,previous)) { invalidTransform_=true; return; }
            const std::array<float,3> delta{matrix._41-previous._41,matrix._42-previous._42,matrix._43-previous._43};
            invalidTransform_=!state.TranslateSelectionWorld(world,delta);
            return;
        }
        if (state.SelectedIds().size()>1)
        {
            ApplyMultipleTransform(world,state,current,matrix);
            return;
        }
        auto transformed=current;
        const bool valid=mode_==Mode::Rotate ? GizmoTransform::ReadRotation(world,current,matrix,transformed) :
            GizmoTransform::ReadScale(current,matrix,transformed);
        invalidTransform_=!valid;
        if (invalidTransform_) return;
        if (transformed.position==current.position && transformed.rotation==current.rotation && transformed.scale==current.scale) return;
        invalidTransform_=!state.SetLocalTransform(world,current.id,transformed.position,transformed.rotation,transformed.scale);
    }

    void TransformGizmo::ApplyMultipleTransform(SceneRuntime::SceneWorld& world, EditState& state,
        const SceneRuntime::ScenePlacement& current, const DirectX::XMFLOAT4X4& matrix)
    {
        DirectX::XMFLOAT4X4 previous,delta;
        if (!world.WorldMatrix(current.id,previous)) { invalidTransform_=true; return; }
        const std::array<float,3> pivot{previous._41,previous._42,previous._43};
        if (mode_==Mode::Rotate)
        {
            if (!GizmoTransform::RotationDelta(world,current,matrix,delta)) { invalidTransform_=true; return; }
            invalidTransform_=!state.RotateSelectionWorld(world,pivot,delta);
            return;
        }
        std::array<float,3> factors;
        if (!GizmoTransform::ScaleDelta(world,current,matrix,factors) || !world.WorldRotation(current.id,delta))
        { invalidTransform_=true; return; }
        invalidTransform_=!state.ScaleSelectionWorld(world,pivot,delta,factors);
    }

}
