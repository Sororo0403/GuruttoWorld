#include <SceneRuntime/Rendering/RenderExtractor.h>
#include <SceneRuntime/Scene/SceneTransforms.h>
#include <Engine/Core/Profiler.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace
{
    using namespace SceneRuntime;
    DirectX::XMMATRIX Rotation(const LocalTransform& value)
    {
        return DirectX::XMMatrixRotationX(value.rotation[0]) * DirectX::XMMatrixRotationY(value.rotation[1]) *
               DirectX::XMMatrixRotationZ(value.rotation[2]);
    }
    bool Valid(const RenderBounds& bounds)
    {
        for (size_t axis = 0; axis < 3; ++axis)
            if (!std::isfinite(bounds.center[axis]) || !std::isfinite(bounds.extent[axis]) || bounds.extent[axis] < 0)
                return false;
        return true;
    }
    bool Finite(const DirectX::XMFLOAT4X4& matrix)
    {
        for (const auto& row : matrix.m)
            for (const auto number : row)
                if (!std::isfinite(number))
                    return false;
        return true;
    }
    bool Interpolate(WorldState::ReadView view, EntityHandle entity, const RuntimePhysicalPose* previous,
                     const RuntimePhysicalPose* current, double interpolation, DirectX::XMFLOAT4X4& matrix,
                     DirectX::XMFLOAT4X4& rotation)
    {
        const auto body = view.ComponentLease<RigidBodyComponent>(entity);
        const auto controller = view.ComponentLease<PlayerControllerComponent>(entity);
        if (!(body && body->enabled && body->motion != "static") && !(controller && controller->enabled))
            return false;
        if (!previous || !current || previous->entity != entity || current->entity != entity ||
            previous->body != current->body || previous->controller != current->controller ||
            (current->body.generation && !view.Alive(current->body)) ||
            (current->controller.generation && !view.Alive(current->controller)))
            return false;
        const auto transform = *view.Transform(entity);
        const auto parent = view.Parent(entity).value_or(EntityHandle{});
        if (parent != previous->parent || parent != current->parent || transform != current->transform)
            return false;
        float distance = 0;
        for (size_t axis = 0; axis < 3; ++axis)
        {
            const auto delta = current->transform.position[axis] - previous->transform.position[axis];
            distance += delta * delta;
            if (std::abs(previous->transform.scale[axis] - current->transform.scale[axis]) >
                1e-5f * std::max(1.f, std::max(std::abs(previous->transform.scale[axis]),
                                               std::abs(current->transform.scale[axis]))))
                return false;
        }
        if (distance > 4)
            return false;
        using namespace DirectX;
        const auto alpha = static_cast<float>(interpolation);
        const auto quaternion = XMQuaternionSlerp(XMQuaternionRotationMatrix(Rotation(previous->transform)),
                                                  XMQuaternionRotationMatrix(Rotation(transform)), alpha);
        const auto orientation = XMMatrixRotationQuaternion(quaternion);
        std::array<float, 3> position;
        for (size_t axis = 0; axis < 3; ++axis)
            position[axis] = previous->transform.position[axis] +
                             (transform.position[axis] - previous->transform.position[axis]) * alpha;
        XMStoreFloat4x4(&rotation, orientation);
        XMStoreFloat4x4(&matrix, XMMatrixScaling(transform.scale[0], transform.scale[1], transform.scale[2]) *
                                     orientation * XMMatrixTranslation(position[0], position[1], position[2]));
        return true;
    }
}
namespace SceneRuntime
{
    std::shared_ptr<const RenderPacket> RenderExtractor::Extract(WorldState::ReadView view,
                                                                 RenderResourceResolver& resources, std::uint64_t frame,
                                                                 std::uint64_t device,
                                                                 const RuntimePhysicalHistory& history,
                                                                 double interpolation, std::string& error)
    {
        try
        {
            Engine::CpuScope scope("Render packet extraction");
            if (!device || !std::isfinite(interpolation) || interpolation < 0 || interpolation > 1)
                throw std::runtime_error("Invalid render packet generation or interpolation");
            auto result = std::make_shared<RenderPacket>();
            result->sceneResources = resources.Scene();
            result->frame = frame;
            result->device = device;
            const auto& settings = view.Definition()->Source().settings;
            result->settings = {settings.mainCamera,     settings.fog,       settings.postEffects,
                                settings.background,     settings.skyShader, settings.oceanShader,
                                settings.particleShader, settings.postShader};
            if (view.With<MeshRendererComponent>().empty() && view.With<TerrainComponent>().empty() &&
                view.With<TilemapComponent>().empty() && view.With<CameraComponent>().empty() &&
                view.With<DirectionalLightComponent>().empty() && view.With<PointLightComponent>().empty() &&
                view.With<SpotLightComponent>().empty() && view.With<SkyComponent>().empty() &&
                view.With<ParticleEmitterComponent>().empty() && view.With<CanvasComponent>().empty() &&
                view.With<RectTransformComponent>().empty())
            {
                error.clear();
                return result;
            }
            const auto entities = view.Entities();
            size_t slots = 0;
            for (const auto entity : entities)
                slots = std::max(slots, static_cast<size_t>(entity.slot) + 1);
            std::vector<DirectX::XMFLOAT4X4> matrices(slots), rotations(slots);
            std::vector<unsigned char> visited(slots);
            std::vector<EntityHandle> chain;
            std::vector<const RuntimePhysicalPose*> previous(slots), current(slots);
            for (const auto& pose : history.previous)
                if (view.Alive(pose.entity))
                    previous[pose.entity.slot] = &pose;
            for (const auto& pose : history.current)
                if (view.Alive(pose.entity))
                    current[pose.entity.slot] = &pose;
            for (const auto entity : entities)
            {
                auto next = entity;
                chain.clear();
                while (next && !visited[next.slot])
                {
                    visited[next.slot] = 1;
                    chain.push_back(next);
                    next = view.Parent(next).value_or(EntityHandle{});
                }
                if (next && visited[next.slot] == 1)
                    throw std::runtime_error("Render hierarchy cycle");
                while (!chain.empty())
                {
                    const auto owner = chain.back();
                    chain.pop_back();
                    auto& matrix = matrices[owner.slot];
                    auto& rotation = rotations[owner.slot];
                    const auto transform = *view.Transform(owner);
                    if (!Interpolate(view, owner, previous[owner.slot], current[owner.slot], interpolation, matrix,
                                     rotation))
                    {
                        if (!SceneTransforms::Compose(transform, matrix))
                            throw std::runtime_error("Invalid render transform");
                        DirectX::XMStoreFloat4x4(&rotation, Rotation(transform));
                    }
                    if (const auto parent = view.Parent(owner); parent && *parent)
                    {
                        DirectX::XMStoreFloat4x4(&matrix, DirectX::XMLoadFloat4x4(&matrix) *
                                                              DirectX::XMLoadFloat4x4(&matrices[parent->slot]));
                        DirectX::XMStoreFloat4x4(&rotation, DirectX::XMLoadFloat4x4(&rotation) *
                                                                DirectX::XMLoadFloat4x4(&rotations[parent->slot]));
                    }
                    if (!SceneTransforms::IsUsable(matrix))
                        throw std::runtime_error("Invalid render world matrix");
                    visited[owner.slot] = 2;
                }
            }
            std::unordered_set<std::uint32_t> uiOwners;
            const auto includeUi = [&](EntityHandle entity)
            {
                while (entity && uiOwners.insert(entity.slot).second)
                    entity = view.Parent(entity).value_or(EntityHandle{});
            };
            for (const auto entity : view.With<CanvasComponent>())
                includeUi(entity);
            for (const auto entity : view.With<RectTransformComponent>())
                includeUi(entity);
            if (!uiOwners.empty())
            {
                auto ui = std::make_shared<UiPacket>();
                for (const auto entity : entities)
                {
                    if (!uiOwners.contains(entity.slot))
                        continue;
                    UiNode node;
                    node.id = *view.ObjectId(entity);
                    if (const auto parent = view.Parent(entity))
                        node.parentId = *view.ObjectId(*parent);
                    node.matrix = matrices[entity.slot];
                    if (const auto canvas = view.ComponentLease<CanvasComponent>(entity))
                        node.canvas = UiCanvas{canvas->enabled, canvas->referenceSize, canvas->scaleWithScreen,
                                               canvas->stateDefaults};
                    node.rectTransform = view.Component<RectTransformComponent>(entity);
                    node.image = view.Component<ImageComponent>(entity);
                    node.text = view.Component<TextComponent>(entity);
                    node.button = view.Component<ButtonComponent>(entity);
                    node.inputField = view.Component<InputFieldComponent>(entity);
                    node.slider = view.Component<SliderComponent>(entity);
                    node.toggle = view.Component<ToggleComponent>(entity);
                    node.scrollView = view.Component<ScrollViewComponent>(entity);
                    node.mask = view.Component<MaskComponent>(entity);
                    node.layoutGroup = view.Component<LayoutGroupComponent>(entity);
                    node.animation = view.Component<AnimationComponent>(entity);
                    ui->objects.push_back(std::move(node));
                }
                result->ui = std::move(ui);
            }
            const auto append = [&](auto& target, auto component, EntityHandle entity)
            {
                if (component && component->enabled)
                    target.push_back(
                        {entity, *view.ObjectId(entity), matrices[entity.slot], rotations[entity.slot], *component});
            };
            for (const auto entity : entities)
            {
                append(result->cameras, view.ComponentLease<CameraComponent>(entity), entity);
                append(result->cameraSways, view.ComponentLease<CameraSwayComponent>(entity), entity);
                append(result->skies, view.ComponentLease<SkyComponent>(entity), entity);
                append(result->particles, view.ComponentLease<ParticleEmitterComponent>(entity), entity);
                append(result->directionalLights, view.ComponentLease<DirectionalLightComponent>(entity), entity);
                const auto pointCount = result->pointLights.size(), spotCount = result->spotLights.size();
                append(result->pointLights, view.ComponentLease<PointLightComponent>(entity), entity);
                append(result->spotLights, view.ComponentLease<SpotLightComponent>(entity), entity);
                if (result->pointLights.size() != pointCount)
                    result->localLights.push_back({false, pointCount});
                if (result->spotLights.size() != spotCount)
                    result->localLights.push_back({true, spotCount});
                const auto mesh = view.ComponentLease<MeshRendererComponent>(entity);
                const auto terrain = view.ComponentLease<TerrainComponent>(entity);
                const auto tilemap = view.ComponentLease<TilemapComponent>(entity);
                if (!(mesh ? mesh->enabled : (terrain && terrain->enabled) || (tilemap && tilemap->enabled)))
                    continue;
                auto binding = resources.Resolve(entity, error);
                if (!binding || !binding->resource || binding->device != device || !binding->generation)
                    throw std::runtime_error(error.empty() ? "Render resource generation missing" : error);
                if (!Valid(binding->bounds))
                    throw std::runtime_error("Invalid render bounds");
                if (binding->skin)
                {
                    if (!Valid(binding->skin->bounds))
                        throw std::runtime_error("Invalid render skin bounds");
                    for (const auto& palette : binding->skin->palettes)
                        for (const auto& matrix : palette)
                            if (!Finite(matrix.position) || !Finite(matrix.normal))
                                throw std::runtime_error("Invalid render skin palette");
                    binding->bounds = binding->skin->bounds;
                }
                RenderObject object;
                object.entity = entity;
                object.id = *view.ObjectId(entity);
                object.matrix = matrices[entity.slot];
                object.rotation = rotations[entity.slot];
                object.binding = std::move(*binding);
                if (mesh)
                {
                    object.visibleWhen = mesh->visibleWhen;
                    object.instancing = mesh->instancing;
                    object.occlusion = mesh->occlusionCulling;
                    for (const auto& lod : mesh->lods)
                        object.lodDistances.push_back(lod.distance);
                }
                result->objects.push_back(std::move(object));
            }
            error.clear();
            return result;
        }
        catch (const std::exception& failure)
        {
            error = failure.what();
            return {};
        }
        catch (...)
        {
            error = "Render extraction failed with an unknown exception";
            return {};
        }
    }
}
