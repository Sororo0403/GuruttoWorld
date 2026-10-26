#include <SceneRuntime/Gameplay/RuntimeNavigation.h>
#include "NavigationMesh.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace
{
    using namespace SceneRuntime;
    using Point = std::array<float, 3>;
    Point Transform(const Point& point, DirectX::FXMMATRIX matrix)
    {
        DirectX::XMFLOAT3 value;
        DirectX::XMStoreFloat3(
            &value, DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(point[0], point[1], point[2], 1), matrix));
        return {value.x, value.y, value.z};
    }
    Point Follow(Point current, const std::vector<Point>& path, double remaining)
    {
        for (size_t index = path.size() > 1 ? 1 : 0; index < path.size() && remaining > 0; ++index)
        {
            const auto& target = path[index];
            const auto distance = std::hypot(target[0] - current[0], target[1] - current[1], target[2] - current[2]);
            if (distance <= remaining)
            {
                current = target;
                remaining -= distance;
            }
            else
            {
                const auto factor = static_cast<float>(remaining / distance);
                for (size_t axis = 0; axis < 3; ++axis)
                    current[axis] = std::lerp(current[axis], target[axis], factor);
                remaining = 0;
            }
        }
        return current;
    }
    struct NavigationFrame final : RuntimeSystem::Frame
    {
        WorldState::Transaction& transaction;
        explicit NavigationFrame(WorldState::Transaction& value) : transaction(value) {}
        bool Fixed(double seconds, const RuntimeInput&, const RuntimeScriptEffects&, std::string& error) override
        {
            return RuntimeNavigation::Advance(transaction, seconds, error);
        }
        bool Update(double, const RuntimeScriptEffects&, std::string&) override
        {
            return true;
        }
        bool Prepare(const SceneChangeSet&, const RuntimeScriptEffects&, std::string&) override
        {
            return true;
        }
        void Commit() noexcept override {}
        void Finish() noexcept override {}
    };
}
namespace SceneRuntime
{
    std::vector<std::array<float, 3>> RuntimeNavigation::Path(WorldState::ReadView view, EntityHandle entity,
                                                              const std::array<float, 3>& start,
                                                              const std::array<float, 3>& destination)
    {
        const auto mesh = view.ComponentLease<NavMeshComponent>(entity);
        if (!mesh || !mesh->enabled || !mesh->baked)
            return {};
        const auto matrix = view.WorldMatrix(entity);
        if (!matrix)
            return {};
        const auto world = DirectX::XMLoadFloat4x4(&*matrix);
        const auto inverse = DirectX::XMMatrixInverse(nullptr, world);
        auto result = NavigationMesh::Path(*mesh, Transform(start, inverse), Transform(destination, inverse));
        for (auto& point : result)
            point = Transform(point, world);
        return result;
    }
    bool RuntimeNavigation::Advance(WorldState::Transaction& transaction, double seconds, std::string& error)
    {
        try
        {
            if (!std::isfinite(seconds) || seconds <= 0)
                throw std::runtime_error("Invalid navigation delta");
            const auto view = transaction.Read();
            struct Target
            {
                EntityHandle entity;
                Point position;
            };
            std::vector<Target> targets;
            std::vector<EntityHandle> stopped;
            std::unordered_map<std::uint32_t, Point> moves;
            for (const auto entity : view.With<NavAgentComponent>())
            {
                const auto agent = view.ComponentLease<NavAgentComponent>(entity);
                if (!agent->enabled || !agent->moving)
                    continue;
                const auto matrix = *view.WorldMatrix(entity);
                const Point start{matrix._41, matrix._42, matrix._43};
                if (std::hypot(start[0] - agent->destination[0], start[1] - agent->destination[1],
                               start[2] - agent->destination[2]) <= agent->stoppingDistance)
                {
                    stopped.push_back(entity);
                    continue;
                }
                const auto mesh = view.Resolve(agent->mesh);
                if (!mesh)
                    continue;
                const auto path = Path(view, *mesh, start, agent->destination);
                if (path.empty())
                    continue;
                const auto current = Follow(start, path, agent->speed * seconds);
                const auto& destination = path.back();
                if (std::hypot(current[0] - destination[0], current[1] - destination[1], current[2] - destination[2]) <=
                    agent->stoppingDistance)
                    stopped.push_back(entity);
                targets.push_back({entity, current});
                moves.emplace(entity.slot, Point{current[0] - start[0], current[1] - start[1], current[2] - start[2]});
            }
            // 書き込み前にすべての親行列を確定し、定義順による二重移動を防ぎます。
            for (auto& target : targets)
            {
                const auto parent = view.Parent(target.entity);
                if (!parent)
                    continue;
                auto matrix = *view.WorldMatrix(*parent);
                auto ancestor = parent;
                while (ancestor && !moves.contains(ancestor->slot))
                    ancestor = view.Parent(*ancestor);
                const auto delta = ancestor ? moves.at(ancestor->slot) : Point{};
                matrix._41 += delta[0];
                matrix._42 += delta[1];
                matrix._43 += delta[2];
                target.position =
                    Transform(target.position, DirectX::XMMatrixInverse(nullptr, DirectX::XMLoadFloat4x4(&matrix)));
            }
            for (const auto& target : targets)
            {
                auto pose = *view.Transform(target.entity);
                pose.position = target.position;
                if (!transaction.SetLocalTransform(target.entity, pose, error).succeeded)
                    return false;
            }
            for (const auto entity : stopped)
            {
                auto agent = view.Component<NavAgentComponent>(entity);
                agent->moving = false;
                if (!transaction.SetComponent(entity, std::move(agent), error).succeeded)
                    return false;
            }
            error.clear();
            return true;
        }
        catch (const std::exception& failure)
        {
            error = failure.what();
            return false;
        }
    }
    std::unique_ptr<RuntimeSystem::Frame> RuntimeNavigation::Begin(WorldState::Transaction& transaction)
    {
        return std::make_unique<NavigationFrame>(transaction);
    }
    RuntimeScriptQueries RuntimeNavigation::Queries() const
    {
        return {};
    }
}
