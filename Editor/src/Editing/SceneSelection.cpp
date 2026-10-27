#include "SceneSelection.h"
#include "../Viewport/ComponentGuideGeometry.h"
#include <imgui.h>
#include <cmath>

namespace
{
    bool ClipEdge(DirectX::XMFLOAT4& a, DirectX::XMFLOAT4& b)
    {
        // D3Dの同次クリップ空間。カメラ後方や画面外へ飛び出した辺も切り詰めます。
        const auto plane = [](const DirectX::XMFLOAT4& p, int index)
        {
            switch (index)
            {
            case 0:
                return p.x + p.w;
            case 1:
                return p.w - p.x;
            case 2:
                return p.y + p.w;
            case 3:
                return p.w - p.y;
            case 4:
                return p.z;
            default:
                return p.w - p.z;
            }
        };
        for (int index = 0; index < 6; ++index)
        {
            const float first = plane(a, index), second = plane(b, index);
            if (first < 0 && second < 0)
                return false;
            if (first < 0 || second < 0)
            {
                const float t = first / (first - second);
                const DirectX::XMFLOAT4 point{std::lerp(a.x, b.x, t), std::lerp(a.y, b.y, t), std::lerp(a.z, b.z, t),
                                              std::lerp(a.w, b.w, t)};
                if (first < 0)
                    a = point;
                else
                    b = point;
            }
        }
        return a.w > 0.000001f && b.w > 0.000001f;
    }
}

namespace Editor
{
    static void DrawGuideLines(ImDrawList* draw, const ComponentGuideGeometry::Lines& segments,
                               const DirectX::XMFLOAT4X4& transform, const Engine::Camera& camera,
                               const SceneViewport& viewport, ImU32 color)
    {
        const auto projection =
            DirectX::XMLoadFloat4x4(&transform) * camera.GetViewMatrix() * camera.GetProjectionMatrix();
        for (const auto& segment : segments)
        {
            DirectX::XMFLOAT4 a, b;
            const auto project = [&](const auto& point, auto& clip)
            {
                DirectX::XMStoreFloat4(&clip, DirectX::XMVector4Transform(
                                                  DirectX::XMVectorSet(point[0], point[1], point[2], 1), projection));
            };
            project(segment[0], a);
            project(segment[1], b);
            if (!ClipEdge(a, b))
                continue;
            const auto first = viewport.ToScreen(a.x / a.w, a.y / a.w), last = viewport.ToScreen(b.x / b.w, b.y / b.w);
            draw->AddLine({first[0], first[1]}, {last[0], last[1]}, color, 1.5f);
        }
    }
    static void DrawGuides(const SceneRuntime::SceneViewQueries& view, const Engine::Camera& camera,
                           const std::string& id, const SceneViewport& viewport)
    {
        if (!viewport.Valid())
            return;
        const auto& objects = view.Layout().objects;
        const auto found = std::find_if(objects.begin(), objects.end(),
                                        [&](const auto& object)
                                        {
                                            return object.id == id;
                                        });
        DirectX::XMFLOAT4X4 matrix{}, rotation{};
        if (found == objects.end() || !view.WorldMatrix(id, matrix))
            return;
        auto* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect({viewport.x, viewport.y}, {viewport.x + viewport.width, viewport.y + viewport.height}, true);

        if (found->boxCollider && found->boxCollider->enabled)
        {
            DirectX::XMVECTOR scale, q, position;
            DirectX::XMFLOAT3 values{1, 1, 1};
            if (DirectX::XMMatrixDecompose(&scale, &q, &position, DirectX::XMLoadFloat4x4(&matrix)))
                DirectX::XMStoreFloat3(&values, scale);
            DrawGuideLines(draw, ComponentGuideGeometry::Collider(*found->boxCollider, {values.x, values.y, values.z}),
                           matrix, camera, viewport, IM_COL32(100, 255, 140, 255));
        }
        if (view.WorldRotation(id, rotation))
        {
            rotation._41 = matrix._41;
            rotation._42 = matrix._42;
            rotation._43 = matrix._43;
            if (found->camera && found->camera->enabled)
                DrawGuideLines(draw, ComponentGuideGeometry::Camera(*found->camera), rotation, camera, viewport,
                               IM_COL32(120, 180, 255, 255));
            if (found->pointLight && found->pointLight->enabled)
                DrawGuideLines(draw, ComponentGuideGeometry::Sphere(found->pointLight->range), rotation, camera,
                               viewport, IM_COL32(255, 210, 100, 255));
            if (found->spotLight && found->spotLight->enabled)
                DrawGuideLines(draw, ComponentGuideGeometry::Spot(*found->spotLight), rotation, camera, viewport,
                               IM_COL32(255, 210, 100, 255));
        }
        draw->PopClipRect();
    }
    void SceneSelection::Update(const SceneRuntime::SceneViewQueries& view, const Engine::Camera& camera,
                                EditState& state, const SceneViewport& viewport, bool active)
    {
        const auto& io = ImGui::GetIO();
        if (!active || !ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
            !viewport.Contains(io.MousePos.x, io.MousePos.y))
            return;
        using namespace DirectX;
        const auto ndc = *viewport.ToNdc(io.MousePos.x, io.MousePos.y);
        const float x = ndc[0], y = ndc[1];
        const auto inverse = XMMatrixInverse(nullptr, camera.GetViewMatrix() * camera.GetProjectionMatrix());
        const auto nearPoint = XMVector3TransformCoord(XMVectorSet(x, y, 0, 1), inverse);
        const auto farPoint = XMVector3TransformCoord(XMVectorSet(x, y, 1, 1), inverse);
        XMFLOAT3 origin, direction;
        XMStoreFloat3(&origin, nearPoint);
        XMStoreFloat3(&direction, farPoint - nearPoint);
        const float distance = XMVectorGetX(XMVector3Length(farPoint - nearPoint));
        state.Select(view.PickRay({origin.x, origin.y, origin.z}, {direction.x, direction.y, direction.z}, distance)
                         .value_or(""),
                     io.KeyCtrl || io.KeyShift);
        ImGui::SetWindowFocus(nullptr);
    }

    void SceneSelection::Draw(const SceneRuntime::SceneViewQueries& view, const Engine::Camera& camera,
                              const EditState& state, const SceneViewport& viewport)
    {
        for (const auto& id : state.SelectedIds())
            DrawObject(view, camera, id, viewport, id == state.SelectedId());
    }

    void SceneSelection::DrawObject(const SceneRuntime::SceneViewQueries& view, const Engine::Camera& camera,
                                    const std::string& id, const SceneViewport& viewport, bool primary)
    {
        DrawGuides(view, camera, id, viewport);
        std::array<std::array<float, 3>, 8> corners;
        if (!view.WorldBounds(id, corners))
            return;
        if (!viewport.Valid())
            return;
        std::array<DirectX::XMFLOAT4, 8> clip;
        const auto viewProjection = camera.GetViewMatrix() * camera.GetProjectionMatrix();
        for (size_t index = 0; index < corners.size(); ++index)
        {
            const auto& point = corners[index];
            DirectX::XMStoreFloat4(
                &clip[index],
                DirectX::XMVector4Transform(DirectX::XMVectorSet(point[0], point[1], point[2], 1), viewProjection));
        }
        constexpr int Edges[12][2]{{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                                   {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        auto* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(ImVec2(viewport.x, viewport.y),
                           ImVec2(viewport.x + viewport.width, viewport.y + viewport.height), true);
        for (const auto* edge : Edges)
        {
            auto a = clip[edge[0]], b = clip[edge[1]];
            if (!ClipEdge(a, b))
                continue;
            const auto screen = [&](const DirectX::XMFLOAT4& p)
            {
                const auto point = viewport.ToScreen(p.x / p.w, p.y / p.w);
                return ImVec2(point[0], point[1]);
            };
            draw->AddLine(screen(a), screen(b), primary ? IM_COL32(255, 210, 50, 255) : IM_COL32(70, 200, 255, 255),
                          2.0f);
        }
        draw->PopClipRect();
    }
}
