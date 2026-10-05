#include "SceneSelection.h"
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
            case 0: return p.x + p.w;
            case 1: return p.w - p.x;
            case 2: return p.y + p.w;
            case 3: return p.w - p.y;
            case 4: return p.z;
            default: return p.w - p.z;
            }
        };
        for (int index = 0; index < 6; ++index)
        {
            const float first = plane(a, index), second = plane(b, index);
            if (first < 0 && second < 0) return false;
            if (first < 0 || second < 0)
            {
                const float t = first / (first - second);
                const DirectX::XMFLOAT4 point{ std::lerp(a.x, b.x, t), std::lerp(a.y, b.y, t),
                    std::lerp(a.z, b.z, t), std::lerp(a.w, b.w, t) };
                if (first < 0) a = point; else b = point;
            }
        }
        return a.w > 0.000001f && b.w > 0.000001f;
    }
}

namespace Editor
{
    void SceneSelection::Update(const SceneRuntime::SceneWorld& world, const Engine::Camera& camera,
        EditState& state, const SceneViewport& viewport, bool active)
    {
        const auto& io = ImGui::GetIO();
        if (!active || io.WantCaptureMouse || ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) ||
            !ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
            !viewport.Contains(io.MousePos.x, io.MousePos.y)) return;
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
        state.Select(world.PickRay({ origin.x, origin.y, origin.z },
            { direction.x, direction.y, direction.z }, distance).value_or(""));
        ImGui::SetWindowFocus(nullptr);
    }

    void SceneSelection::Draw(const SceneRuntime::SceneWorld& world, const Engine::Camera& camera,
        const EditState& state, const SceneViewport& viewport)
    {
        std::array<std::array<float, 3>, 8> corners;
        if (!world.WorldBounds(state.SelectedId(), corners)) return;
        if (!viewport.Valid()) return;
        std::array<DirectX::XMFLOAT4, 8> clip;
        const auto viewProjection = camera.GetViewMatrix() * camera.GetProjectionMatrix();
        for (size_t index = 0; index < corners.size(); ++index)
        {
            const auto& point = corners[index];
            DirectX::XMStoreFloat4(&clip[index], DirectX::XMVector4Transform(
                DirectX::XMVectorSet(point[0], point[1], point[2], 1), viewProjection));
        }
        constexpr int Edges[12][2]{ {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},
            {0,4},{1,5},{2,6},{3,7} };
        auto* draw = ImGui::GetBackgroundDrawList();
        draw->PushClipRect(ImVec2(viewport.x, viewport.y),
            ImVec2(viewport.x + viewport.width, viewport.y + viewport.height), true);
        for (const auto* edge : Edges)
        {
            auto a = clip[edge[0]], b = clip[edge[1]];
            if (!ClipEdge(a, b)) continue;
            const auto screen = [&](const DirectX::XMFLOAT4& p)
            {
                const auto point = viewport.ToScreen(p.x / p.w, p.y / p.w);
                return ImVec2(point[0], point[1]);
            };
            draw->AddLine(screen(a), screen(b), IM_COL32(255, 210, 50, 255), 2.0f);
        }
        draw->PopClipRect();
    }
}
