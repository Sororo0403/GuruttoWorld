#include <SceneRuntime/Physics/ScenePhysics.h>
#include "PhysicsFrame.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    using namespace SceneRuntime;
    using Matrix = DirectX::XMFLOAT4X4;
    struct Bounds
    {
        std::array<float, 3> low, high;
    };
    Bounds Box(const BoxColliderComponent& box, const Matrix& matrix)
    {
        Bounds result;
        result.low.fill(std::numeric_limits<float>::max());
        result.high.fill(std::numeric_limits<float>::lowest());
        for (int corner = 0; corner < 8; ++corner)
        {
            auto point = box.center;
            for (size_t axis = 0; axis < 3; ++axis)
                point[axis] += box.size[axis] * ((corner & (1 << axis)) ? 0.5f : -0.5f);
            DirectX::XMFLOAT3 world;
            DirectX::XMStoreFloat3(
                &world, DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(point[0], point[1], point[2], 1),
                                                         DirectX::XMLoadFloat4x4(&matrix)));
            const std::array<float, 3> values{world.x, world.y, world.z};
            for (size_t axis = 0; axis < 3; ++axis)
            {
                result.low[axis] = std::min(result.low[axis], values[axis]);
                result.high[axis] = std::max(result.high[axis], values[axis]);
            }
        }
        return result;
    }
    bool Related(const PhysicsFrame& layout, size_t first, size_t second)
    {
        const auto ancestor = [&](size_t child, size_t parent)
        {
            auto id = layout.objects[child].parentId;
            while (!id.empty())
            {
                if (id == layout.objects[parent].id)
                    return true;
                const auto found = std::find_if(layout.objects.begin(), layout.objects.end(),
                                                [&](const auto& object)
                                                {
                                                    return object.id == id;
                                                });
                if (found == layout.objects.end())
                    break;
                id = found->parentId;
            }
            return false;
        };
        return first == second || ancestor(first, second) || ancestor(second, first);
    }
    bool OverlapSides(const Bounds& moving, const Bounds& fixed, size_t axis)
    {
        bool overlap = true;
        for (size_t side = 0; side < 3; ++side)
            if (side != axis &&
                (moving.high[side] <= fixed.low[side] + 0.00001f || moving.low[side] >= fixed.high[side] - 0.00001f))
                overlap = false;
        return overlap;
    }
    bool SweepCollider(const PhysicsFrame& layout, size_t index, size_t other)
    {
        const auto& collider = layout.objects[other].boxCollider;
        if (!collider || !collider->enabled || collider->isTrigger || Related(layout, index, other))
            return false;
        const auto& own = *layout.objects[index].boxCollider;
        if (!(own.mask & (1u << collider->layer)) || !(collider->mask & (1u << own.layer)))
            return false;
        return true;
    }

    float Sweep(const PhysicsFrame& layout, const std::vector<Matrix>& matrices, size_t index, size_t axis,
                float distance)
    {
        const auto& object = layout.objects[index];
        if (!object.boxCollider || !object.boxCollider->enabled || object.boxCollider->isTrigger)
            return distance;
        const auto moving = Box(*object.boxCollider, matrices[index]);
        for (size_t other = 0; other < layout.objects.size(); ++other)
        {
            if (!SweepCollider(layout, index, other))
                continue;
            const auto& collider = layout.objects[other].boxCollider;
            const auto fixed = Box(*collider, matrices[other]);
            const bool overlap = OverlapSides(moving, fixed, axis);
            if (!overlap)
                continue;
            if (distance > 0 && moving.high[axis] <= fixed.low[axis] + 0.00001f)
                distance = std::min(distance, std::max(0.0f, fixed.low[axis] - moving.high[axis]));
            if (distance < 0 && moving.low[axis] >= fixed.high[axis] - 0.00001f)
                distance = std::max(distance, std::min(0.0f, fixed.high[axis] - moving.low[axis]));
        }
        return distance;
    }
    DirectX::XMMATRIX Parent(const PhysicsFrame& layout, const std::vector<Matrix>& matrices, size_t index)
    {
        const auto& id = layout.objects[index].parentId;
        if (id.empty())
            return DirectX::XMMatrixIdentity();
        const auto found = std::find_if(layout.objects.begin(), layout.objects.end(),
                                        [&](const auto& object)
                                        {
                                            return object.id == id;
                                        });
        return DirectX::XMLoadFloat4x4(&matrices[static_cast<size_t>(found - layout.objects.begin())]);
    }
    bool Move(PhysicsFrame& layout, std::vector<Matrix>& matrices, size_t index, size_t axis, float distance)
    {
        if (distance == 0)
            return true;
        std::array<float, 3> vector{};
        vector[axis] = distance;
        DirectX::XMFLOAT3 local;
        DirectX::XMStoreFloat3(&local, DirectX::XMVector3TransformNormal(
                                           DirectX::XMVectorSet(vector[0], vector[1], vector[2], 0),
                                           DirectX::XMMatrixInverse(nullptr, Parent(layout, matrices, index))));
        auto& position = layout.objects[index].position;
        position[0] += local.x;
        position[1] += local.y;
        position[2] += local.z;
        std::string error;
        return PhysicsTransforms::Resolve(layout, matrices, error);
    }
    struct PlayerInput
    {
        float horizontal, vertical, length, tick;
        bool jump;
    };
    bool MoveController(PhysicsFrame& layout, std::vector<Matrix>& matrices, size_t index,
                        const PlayerControllerComponent& controller, const PlayerInput& input)
    {
        const auto& [horizontal, vertical, length, tick, jump] = input;
        DirectX::XMFLOAT3 movement;
        const float inputSpeed = layout.objects[index].animator && layout.objects[index].animator->enabled &&
                                         layout.objects[index].animator->rootMotion
                                     ? 0.0f
                                     : controller.moveSpeed;
        DirectX::XMStoreFloat3(&movement, DirectX::XMVector3TransformNormal(
                                              DirectX::XMVectorSet(horizontal / length * inputSpeed * tick, 0,
                                                                   vertical / length * inputSpeed * tick, 0),
                                              Parent(layout, matrices, index)));
        for (const auto axis : {size_t(0), size_t(2)})
        {
            const float distance = axis == 0 ? movement.x : movement.z;
            if (!Move(layout, matrices, index, axis, Sweep(layout, matrices, index, axis, distance)))
                return false;
        }
        // 親の回転によって、ローカルの水平面に沿う移動にもワールド座標の上下成分が生じます。
        if (!Move(layout, matrices, index, 1, Sweep(layout, matrices, index, 1, movement.y)))
            return false;
        return true;
    }
    bool UsesAdvancedPhysics(const PhysicsValue<PlayerControllerComponent>& controller)
    {
        return controller && controller->enabled && controller->usePhysics;
    }
    bool AdvancePlayer(PhysicsFrame& layout, ScenePhysics::States& states, std::vector<Matrix>& matrices, size_t index,
                       int step, const PlayerInput& input)
    {
        const auto& [horizontal, vertical, length, tick, jump] = input;
        const auto& controller = layout.objects[index].playerController;
        if (UsesAdvancedPhysics(controller))
            return true;
        if (!controller || !controller->enabled)
        {
            states.erase(layout.objects[index].id);
            return true;
        }
        auto& body = states[layout.objects[index].id];
        const bool collider = layout.objects[index].boxCollider && layout.objects[index].boxCollider->enabled;
        body.grounded = collider && Sweep(layout, matrices, index, 1, -0.001f) > -0.001f;
        if (controller->useGravity && step == 0 && jump && body.grounded)
        {
            body.verticalSpeed = controller->jumpSpeed;
            body.grounded = false;
        }
        if (!MoveController(layout, matrices, index, *controller, input))
            return false;
        if (!controller->useGravity)
        {
            body.verticalSpeed = 0;
            return true;
        }
        body.verticalSpeed -= controller->gravity * tick;
        const float requested = body.verticalSpeed * tick;
        const float accepted = Sweep(layout, matrices, index, 1, requested);
        if (!Move(layout, matrices, index, 1, accepted))
            return false;
        if (accepted != requested)
        {
            body.grounded = requested < 0;
            body.verticalSpeed = 0;
        }

        return true;
    }

}
namespace SceneRuntime
{
    bool ScenePhysics::AdvanceFrame(PhysicsFrame& layout, States& states, double seconds, float horizontal,
                                    float vertical, bool jump)
    {
        if (!std::isfinite(seconds) || seconds <= 0 || !std::isfinite(horizontal) || !std::isfinite(vertical))
            return false;
        std::vector<Matrix> matrices;
        std::string error;
        if (!PhysicsTransforms::Resolve(layout, matrices, error))
            return false;
        horizontal = std::clamp(horizontal, -1.0f, 1.0f);
        vertical = std::clamp(vertical, -1.0f, 1.0f);
        const float length = std::max(1.0f, std::hypot(horizontal, vertical));
        const float elapsed = static_cast<float>(std::min(seconds, 0.1));
        const int steps = std::max(1, static_cast<int>(std::ceil(elapsed * 120)));
        const float tick = elapsed / static_cast<float>(steps);
        for (int step = 0; step < steps; ++step)
            for (size_t index = 0; index < layout.objects.size(); ++index)
            {
                if (!AdvancePlayer(layout, states, matrices, index, step, {horizontal, vertical, length, tick, jump}))
                    return false;
            }
        return true;
    }

    bool ScenePhysics::Advance(SceneLayout& layout, States& states, double seconds, float horizontal, float vertical,
                               bool jump)
    {
        auto frame = PhysicsFrame::Capture(layout);
        auto candidateStates = states;
        if (!AdvanceFrame(frame, candidateStates, seconds, horizontal, vertical, jump))
            return false;
        for (size_t index = 0; index < frame.objects.size(); ++index)
        {
            layout.objects[index].position = frame.objects[index].position;
            layout.objects[index].rotation = frame.objects[index].rotation;
            layout.objects[index].scale = frame.objects[index].scale;
        }
        states.swap(candidateStates);
        return true;
    }
}
