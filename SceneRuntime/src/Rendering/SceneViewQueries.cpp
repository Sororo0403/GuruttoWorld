#include <SceneRuntime/Rendering/SceneViewQueries.h>
#include <SceneRuntime/Rendering/SceneRenderState.h>
#include <SceneRuntime/Scene/SceneTransforms.h>
#include <Engine/Core/Log.h>
#include <algorithm>
#include <cmath>

namespace
{
    bool RenderableEnabled(const SceneRuntime::ScenePlacement& p)
    {
        return (p.meshRenderer && p.meshRenderer->enabled) || (p.terrain && p.terrain->enabled) ||
               (p.tilemap && p.tilemap->enabled);
    }
    bool HasPhysicalPose(const SceneRuntime::ScenePlacement& p)
    {
        return (p.playerController && p.playerController->enabled) ||
               (p.rigidBody && p.rigidBody->enabled && p.rigidBody->motion != "static");
    }
}
namespace SceneRuntime
{
    SceneViewQueries::SceneViewQueries(const SceneLayout& layout, const SceneRenderState& renderState,
                                       SceneViewInterpolation interpolation) noexcept
        : layout_(layout), renderState_(renderState), interpolation_(interpolation)
    {
    }
    const SceneLayout& SceneViewQueries::Layout() const noexcept
    {
        return layout_;
    }
    std::optional<std::string> SceneViewQueries::PickRay(const std::array<float, 3>& origin,
                                                         const std::array<float, 3>& direction, float maxDistance) const
    {
        const auto finite = [](const auto& vector)
        {
            return std::all_of(vector.begin(), vector.end(),
                               [](float value)
                               {
                                   return std::isfinite(value);
                               });
        };
        if (!finite(origin) || !finite(direction) || !std::isfinite(maxDistance) || maxDistance <= 0)
            return {};
        using namespace DirectX;
        const auto start = XMVectorSet(origin[0], origin[1], origin[2], 1);
        auto ray = XMVectorSet(direction[0], direction[1], direction[2], 0);
        const float length = XMVectorGetX(XMVector3Length(ray));
        if (!std::isfinite(length) || length <= 0)
            return {};
        ray /= length;
        float closest = maxDistance;
        std::optional<std::string> selected;
        for (size_t index = 0; index < std::min(renderState_.Read().objects.size(), layout_.objects.size()); ++index)
        {
            const auto& object = renderState_.Read().objects[index];
            const auto& placement = layout_.objects[index];
            const bool enabled = RenderableEnabled(placement);
            if (!object.GetModel() || !enabled)
                continue;
            const auto inverse = XMMatrixInverse(nullptr, XMLoadFloat4x4(&object.GetWorldMatrix()));
            const auto localStart = XMVector3TransformCoord(start, inverse);
            auto localRay = XMVector3TransformNormal(ray, inverse);
            const float factor = XMVectorGetX(XMVector3Length(localRay));
            if (!std::isfinite(factor) || factor <= 0)
                continue;
            localRay /= factor;
            float hit;
            if (object.GetModel()->IntersectRay(localStart, localRay, hit) && hit / factor < closest)
            {
                closest = hit / factor;
                selected = layout_.objects[index].id;
            }
        }
        return selected;
    }

    bool SceneViewQueries::InterpolatedLocalTransform(const ScenePlacement& p, DirectX::XMFLOAT4X4& transform,
                                                      DirectX::XMFLOAT4X4& rotation) const
    {
        if (!interpolation_.previous || !interpolation_.current || !HasPhysicalPose(p))
            return false;
        const auto before = interpolation_.previous->find(p.id), now = interpolation_.current->find(p.id);
        if (before == interpolation_.previous->end() || now == interpolation_.current->end())
            return false;
        const auto& a = before->second;
        const auto& b = now->second;
        if (p.position != b.position || p.rotation != b.rotation || p.scale != b.scale || p.parentId != a.parent ||
            p.parentId != b.parent)
            return false;
        float distance = 0;
        for (size_t i = 0; i < 3; ++i)
            distance += (b.position[i] - a.position[i]) * (b.position[i] - a.position[i]);
        if (distance > 4)
            return false;
        // 物理姿勢を SRT に戻す際の微小なスケール誤差は許容します。保存された現在姿勢と異なる直接編集は上の判定で補間から除外します。
        for (size_t i = 0; i < 3; ++i)
            if (std::abs(a.scale[i] - b.scale[i]) >
                1e-5f * std::max(1.0f, std::max(std::abs(a.scale[i]), std::abs(b.scale[i]))))
                return false;
        const float alpha = std::clamp(interpolation_.alpha, 0.0f, 1.0f);
        std::array<float, 3> position;
        for (size_t i = 0; i < 3; ++i)
            position[i] = a.position[i] + (p.position[i] - a.position[i]) * alpha;
        using namespace DirectX;
        const auto localRotation = [](const auto& angles)
        {
            return XMMatrixRotationX(angles[0]) * XMMatrixRotationY(angles[1]) * XMMatrixRotationZ(angles[2]);
        };
        const auto orientation = XMQuaternionSlerp(XMQuaternionRotationMatrix(localRotation(a.rotation)),
                                                   XMQuaternionRotationMatrix(localRotation(p.rotation)), alpha);
        const auto interpolatedRotation = XMMatrixRotationQuaternion(orientation);
        XMStoreFloat4x4(&rotation, interpolatedRotation);
        XMStoreFloat4x4(&transform, XMMatrixScaling(p.scale[0], p.scale[1], p.scale[2]) * interpolatedRotation *
                                        XMMatrixTranslation(position[0], position[1], position[2]));
        return true;
    }
    bool SceneViewQueries::PrepareRenderTransforms() const
    {
        if (renderState_.MatricesCurrent(interpolation_))
            return true;
        std::map<std::string, DirectX::XMFLOAT4X4> overrides;
        for (const auto& p : layout_.objects)
        {
            DirectX::XMFLOAT4X4 matrix, rotation;
            if (InterpolatedLocalTransform(p, matrix, rotation))
                overrides.emplace(p.id, matrix);
        }
        std::string error;
        if (!renderState_.PrepareMatrices(layout_, overrides, error, interpolation_))
        {
            Engine::Log::Warning(error);
            return false;
        }
        return true;
    }

    bool SceneViewQueries::RenderWorldMatrix(std::string_view id, DirectX::XMFLOAT4X4& matrix) const
    {
        if (!PrepareRenderTransforms())
            return false;
        const auto found = std::find_if(layout_.objects.begin(), layout_.objects.end(),
                                        [&](const auto& p)
                                        {
                                            return p.id == id;
                                        });
        if (found == layout_.objects.end())
            return false;
        const auto index = static_cast<size_t>(found - layout_.objects.begin());
        if (index >= renderState_.Matrices().size())
            return false;
        matrix = renderState_.Matrices()[index];
        return true;
    }

    bool SceneViewQueries::WorldRotation(std::string_view id, DirectX::XMFLOAT4X4& matrix) const
    {
        auto rotation = DirectX::XMMatrixIdentity();
        auto current = std::string(id);
        size_t remaining = layout_.objects.size();
        do
        {
            const auto found = std::find_if(layout_.objects.begin(), layout_.objects.end(),
                                            [&](const auto& placement)
                                            {
                                                return placement.id == current;
                                            });
            if (found == layout_.objects.end() || remaining-- == 0)
                return false;
            const auto& angles = found->rotation;
            rotation = rotation * DirectX::XMMatrixRotationX(angles[0]) * DirectX::XMMatrixRotationY(angles[1]) *
                       DirectX::XMMatrixRotationZ(angles[2]);
            current = found->parentId;
        } while (!current.empty());
        DirectX::XMFLOAT4X4 candidate;
        DirectX::XMStoreFloat4x4(&candidate, rotation);
        if (!SceneTransforms::IsUsable(candidate))
            return false;
        matrix = candidate;
        return true;
    }

    bool SceneViewQueries::RenderWorldRotation(std::string_view id, DirectX::XMFLOAT4X4& matrix) const
    {
        auto rotation = DirectX::XMMatrixIdentity();
        auto current = std::string(id);
        size_t remaining = layout_.objects.size();
        do
        {
            const auto found = std::find_if(layout_.objects.begin(), layout_.objects.end(),
                                            [&](const auto& placement)
                                            {
                                                return placement.id == current;
                                            });
            if (found == layout_.objects.end() || remaining-- == 0)
                return false;
            DirectX::XMFLOAT4X4 transform, local;
            if (InterpolatedLocalTransform(*found, transform, local))
                rotation = rotation * DirectX::XMLoadFloat4x4(&local);
            else
            {
                const auto& angles = found->rotation;
                rotation = rotation * DirectX::XMMatrixRotationX(angles[0]) * DirectX::XMMatrixRotationY(angles[1]) *
                           DirectX::XMMatrixRotationZ(angles[2]);
            }
            current = found->parentId;
        } while (!current.empty());
        DirectX::XMFLOAT4X4 candidate;
        DirectX::XMStoreFloat4x4(&candidate, rotation);
        if (!SceneTransforms::IsUsable(candidate))
            return false;
        matrix = candidate;
        return true;
    }

    bool SceneViewQueries::WorldMatrix(std::string_view id, DirectX::XMFLOAT4X4& matrix) const
    {
        const auto found = std::find_if(layout_.objects.begin(), layout_.objects.end(),
                                        [&](const auto& placement)
                                        {
                                            return placement.id == id;
                                        });
        if (found == layout_.objects.end() ||
            static_cast<size_t>(found - layout_.objects.begin()) >= renderState_.Read().objects.size())
            return false;
        matrix = renderState_.Read().objects[static_cast<size_t>(found - layout_.objects.begin())].GetWorldMatrix();
        return true;
    }

    bool SceneViewQueries::SkeletonWorldMatrix(std::string_view id, DirectX::XMFLOAT4X4& matrix) const
    {
        const auto found = std::find_if(layout_.objects.begin(), layout_.objects.end(),
                                        [&](const auto& placement)
                                        {
                                            return placement.id == id;
                                        });
        if (found == layout_.objects.end() ||
            static_cast<size_t>(found - layout_.objects.begin()) >= renderState_.Read().objects.size())
            return false;
        const auto& object = renderState_.Read().objects[static_cast<size_t>(found - layout_.objects.begin())];
        const auto model = object.GetModel();
        if (!model || !model->Rig())
            return false;
        const auto& rig = *model->Rig();
        if (!std::isfinite(rig.importScale) || rig.importScale <= 0)
            return false;
        DirectX::XMFLOAT4X4 candidate;
        DirectX::XMStoreFloat4x4(&candidate,
                                 DirectX::XMLoadFloat4x4(&rig.inverseRoot) *
                                     DirectX::XMMatrixScaling(rig.importScale, rig.importScale, rig.importScale) *
                                     DirectX::XMLoadFloat4x4(&object.GetWorldMatrix()));
        if (!SceneTransforms::IsUsable(candidate))
            return false;
        matrix = candidate;
        return true;
    }

    bool SceneViewQueries::LocalTransformFromWorld(std::string_view id, const DirectX::XMFLOAT4X4& world,
                                                   ScenePlacement& placement) const
    {
        const auto found = std::find_if(layout_.objects.begin(), layout_.objects.end(),
                                        [&](const auto& value)
                                        {
                                            return value.id == id;
                                        });
        if (found == layout_.objects.end())
            return false;
        auto matrix = world;
        if (!found->parentId.empty())
        {
            DirectX::XMFLOAT4X4 parent;
            if (!WorldMatrix(found->parentId, parent) || !SceneTransforms::WorldToLocal(world, parent, matrix))
                return false;
        }
        return SceneTransforms::ReadTransform(matrix, *found, placement);
    }

    bool SceneViewQueries::WorldBounds(std::string_view id, std::array<std::array<float, 3>, 8>& corners) const
    {
        const auto found = std::find_if(layout_.objects.begin(), layout_.objects.end(),
                                        [id](const auto& placement)
                                        {
                                            return placement.id == id;
                                        });
        if (found == layout_.objects.end() ||
            static_cast<size_t>(found - layout_.objects.begin()) >= renderState_.Read().objects.size())
            return false;
        const auto& object = renderState_.Read().objects[static_cast<size_t>(found - layout_.objects.begin())];
        DirectX::XMFLOAT3 localCorners[8];
        if (object.GetModel())
            object.GetModel()->Bounds().GetCorners(localCorners);
        else
            DirectX::BoundingBox(DirectX::XMFLOAT3{}, DirectX::XMFLOAT3{0.25f, 0.25f, 0.25f}).GetCorners(localCorners);
        const auto matrix = DirectX::XMLoadFloat4x4(&object.GetWorldMatrix());
        std::array<std::array<float, 3>, 8> candidate;
        for (size_t i = 0; i < 8; ++i)
        {
            DirectX::XMFLOAT3 position;
            DirectX::XMStoreFloat3(&position,
                                   DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&localCorners[i]), matrix));
            if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
                return false;
            candidate[i] = {position.x, position.y, position.z};
        }
        corners = candidate;
        return true;
    }
}
