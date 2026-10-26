#include <SceneRuntime/Gameplay/Navigation.h>
#include "NavigationMesh.h"
#include <SceneRuntime/Scene/SceneTransforms.h>
#include <cmath>
#include <algorithm>
#include <stdexcept>
namespace
{
    using namespace SceneRuntime;
    std::array<float, 3> Point(const std::array<float, 3>& point, DirectX::FXMMATRIX matrix)
    {
        DirectX::XMFLOAT3 result;
        DirectX::XMStoreFloat3(
            &result, DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(point[0], point[1], point[2], 1), matrix));
        return {result.x, result.y, result.z};
    }
    using Displacements = std::map<std::string, std::array<float, 3>>;
    std::array<float, 3> ParentDisplacement(std::string id, const std::map<std::string, std::string>& parents,
                                            Displacements& moves)
    {
        // ナビゲーションは平行移動だけを変更します。エージェントでない子孫は、最も近い移動中の祖先のワールド移動量を継承します。
        std::vector<std::string> chain;
        while (!id.empty() && !moves.contains(id))
        {
            chain.push_back(id);
            id = parents.at(id);
        }
        const auto delta = id.empty() ? std::array<float, 3>{} : moves.at(id);
        for (const auto& child : chain)
            moves.emplace(child, delta);
        return delta;
    }
    void LocalAgentPositions(const SceneLayout& layout, const std::vector<DirectX::XMFLOAT4X4>& matrices,
                             Displacements& targets, Displacements moves)
    {
        std::map<std::string, std::string> parents;
        std::map<std::string, size_t> indices;
        for (size_t i = 0; i < layout.objects.size(); ++i)
        {
            parents.emplace(layout.objects[i].id, layout.objects[i].parentId);
            indices.emplace(layout.objects[i].id, i);
        }
        for (const auto& p : layout.objects)
        {
            const auto target = targets.find(p.id);
            if (target == targets.end())
                continue;
            auto parent = DirectX::XMMatrixIdentity();
            if (!p.parentId.empty())
            {
                auto matrix = matrices[indices.at(p.parentId)];
                const auto delta = ParentDisplacement(p.parentId, parents, moves);
                matrix._41 += delta[0];
                matrix._42 += delta[1];
                matrix._43 += delta[2];
                parent = DirectX::XMLoadFloat4x4(&matrix);
            }
            target->second = Point(target->second, DirectX::XMMatrixInverse(nullptr, parent));
        }
    }
    std::array<float, 3> FollowPath(std::array<float, 3> current, const std::vector<std::array<float, 3>>& path,
                                    double remaining)
    {
        for (size_t point = path.size() > 1 ? 1 : 0; point < path.size() && remaining > 0; ++point)
        {
            const auto& target = path[point];
            const float distance = std::hypot(target[0] - current[0], target[1] - current[1], target[2] - current[2]);
            if (distance <= remaining)
            {
                current = target;
                remaining -= distance;
            }
            else
            {
                const float factor = static_cast<float>(remaining / distance);
                for (size_t axis = 0; axis < 3; ++axis)
                    current[axis] = std::lerp(current[axis], target[axis], factor);
                remaining = 0;
            }
        }
        return current;
    }

}
namespace SceneRuntime
{
    bool Navigation::Bake(const SceneLayout& layout, ScenePlacement& owner, std::string& error)
    {
        try
        {
            if (!owner.navMesh)
                throw std::runtime_error("NavMesh required");
            auto candidate = *owner.navMesh;
            if (owner.terrain)
            {
                if (owner.terrain->columns < 2 || owner.terrain->columns > 129 || owner.terrain->rows < 2 ||
                    owner.terrain->rows > 129)
                {
                    throw std::runtime_error("Invalid navigation terrain grid");
                }
                candidate.columns = owner.terrain->columns - 1;
                candidate.rows = owner.terrain->rows - 1;
                candidate.cellSize = owner.terrain->cellSize;
            }
            if (candidate.columns == 0 || candidate.columns > 128 || candidate.rows == 0 || candidate.rows > 128)
            {
                throw std::runtime_error("Invalid navigation grid dimensions");
            }
            candidate.heights.assign(static_cast<size_t>(candidate.columns) * candidate.rows, 0);
            candidate.walkable.assign(candidate.heights.size(), true);
            std::vector<DirectX::XMFLOAT4X4> matrices;
            if (!SceneTransforms::Resolve(layout, matrices, error))
                return false;
            const auto found = std::find_if(layout.objects.begin(), layout.objects.end(),
                                            [&](const auto& p)
                                            {
                                                return p.id == owner.id;
                                            });
            DirectX::XMFLOAT4X4 transform;
            if (found == layout.objects.end())
            {
                if (!SceneTransforms::Compose(owner, transform))
                    throw std::runtime_error("Invalid NavMesh transform");
            }
            else
                transform = matrices[static_cast<size_t>(found - layout.objects.begin())];
            NavigationMesh::Bake(layout, owner, matrices, transform, candidate);
            candidate.baked = true;
            owner.navMesh = std::move(candidate);
            error.clear();
            return true;
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return false;
        }
    }
    std::vector<std::array<float, 3>> Navigation::Path(const SceneLayout& layout, const std::string& id,
                                                       const std::array<float, 3>& start,
                                                       const std::array<float, 3>& destination)
    {
        const auto found = std::find_if(layout.objects.begin(), layout.objects.end(),
                                        [&](const auto& p)
                                        {
                                            return p.id == id;
                                        });
        if (found == layout.objects.end() || !found->navMesh || !found->navMesh->enabled || !found->navMesh->baked)
            return {};
        const auto& mesh = *found->navMesh;
        std::vector<DirectX::XMFLOAT4X4> matrices;
        std::string error;
        if (!SceneTransforms::Resolve(layout, matrices, error))
            return {};
        const auto world = DirectX::XMLoadFloat4x4(&matrices[static_cast<size_t>(found - layout.objects.begin())]);
        const auto inverse = DirectX::XMMatrixInverse(nullptr, world);
        const auto localStart = Point(start, inverse), localEnd = Point(destination, inverse);
        auto result = NavigationMesh::Path(mesh, localStart, localEnd);
        for (auto& point : result)
        {
            point = Point(point, world);
        }
        return result;
    }
    bool Navigation::Advance(SceneLayout& layout, double seconds, std::string& error)
    {
        try
        {
            if (!std::isfinite(seconds) || seconds <= 0)
                throw std::runtime_error("Invalid navigation delta");
            std::vector<DirectX::XMFLOAT4X4> matrices;
            if (!SceneTransforms::Resolve(layout, matrices, error))
                return false;
            // オブジェクトの順序にかかわらず、すべてのエージェントは更新前の同じ階層と NavMesh を使って計画します。
            Displacements targets, moves;
            std::vector<size_t> stopped;
            for (size_t i = 0; i < layout.objects.size(); ++i)
            {
                const auto& p = layout.objects[i];
                if (!p.navAgent || !p.navAgent->enabled || !p.navAgent->moving)
                    continue;
                const auto& agent = *p.navAgent;
                const std::array<float, 3> start{matrices[i]._41, matrices[i]._42, matrices[i]._43};
                if (std::hypot(start[0] - agent.destination[0], start[1] - agent.destination[1],
                               start[2] - agent.destination[2]) <= agent.stoppingDistance)
                {
                    stopped.push_back(i);
                    continue;
                }
                const auto path = Path(layout, agent.mesh, start, agent.destination);
                if (path.empty())
                    continue;
                const auto current = FollowPath(start, path, agent.speed * seconds);
                const auto& destination = path.back();
                if (std::hypot(current[0] - destination[0], current[1] - destination[1], current[2] - destination[2]) <=
                    agent.stoppingDistance)
                    stopped.push_back(i);
                targets.emplace(p.id, current);
                moves.emplace(
                    p.id, std::array<float, 3>{current[0] - start[0], current[1] - start[1], current[2] - start[2]});
            }
            LocalAgentPositions(layout, matrices, targets, std::move(moves));
            for (auto& p : layout.objects)
                if (const auto found = targets.find(p.id); found != targets.end())
                    p.position = found->second;
            for (const auto index : stopped)
                layout.objects[index].navAgent->moving = false;
            error.clear();
            return true;
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return false;
        }
    }
    bool Navigation::HasAgents(const SceneLayout& layout)
    {
        return std::any_of(layout.objects.begin(), layout.objects.end(),
                           [](const auto& p)
                           {
                               return p.navAgent && p.navAgent->enabled && p.navAgent->moving;
                           });
    }
}
