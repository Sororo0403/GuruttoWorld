#include <SceneRuntime/Navigation.h>
#include <iterator>
#include <SceneRuntime/SceneTransforms.h>
#include <queue>
#include <limits>
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
    size_t Cell(const NavMeshComponent& mesh, const std::array<float, 3>& local)
    {
        const int x = static_cast<int>(std::floor(local[0] / mesh.cellSize)),
                  z = static_cast<int>(std::floor(local[2] / mesh.cellSize));
        return x < 0 || z < 0 || static_cast<unsigned int>(x) >= mesh.columns ||
                       static_cast<unsigned int>(z) >= mesh.rows
                   ? SIZE_MAX
                   : static_cast<size_t>(z) * mesh.columns + x;
    }
    float TerrainHeight(const TerrainComponent& terrain, float x, float z)
    {
        const float tx = std::clamp(x / terrain.cellSize, 0.0f, static_cast<float>(terrain.columns - 1)),
                    tz = std::clamp(z / terrain.cellSize, 0.0f, static_cast<float>(terrain.rows - 1));
        const auto a = static_cast<unsigned int>(tx), b = static_cast<unsigned int>(tz),
                   c = std::min(a + 1, terrain.columns - 1), d = std::min(b + 1, terrain.rows - 1);
        const auto h = [&](unsigned int column, unsigned int row)
        {
            return terrain.heights.at(row * terrain.columns + column);
        };
        return std::lerp(std::lerp(h(a, b), h(c, b), tx - a), std::lerp(h(a, d), h(c, d), tx - a), tz - b);
    }
    struct Obstacle
    {
        DirectX::XMFLOAT4X4 inverse;
        BoxColliderComponent box;
    };
    std::vector<Obstacle> Obstacles(const SceneLayout& layout, const std::vector<DirectX::XMFLOAT4X4>& matrices,
                                    const std::string& owner)
    {
        std::vector<Obstacle> result;
        for (size_t i = 0; i < layout.objects.size(); ++i)
        {
            const auto& p = layout.objects[i];
            if (p.id == owner || p.navAgent || !p.boxCollider || !p.boxCollider->enabled || p.boxCollider->isTrigger)
                continue;
            if (p.boxCollider->shape != "box")
                continue;
            DirectX::XMFLOAT4X4 inverse;
            DirectX::XMStoreFloat4x4(&inverse,
                                     DirectX::XMMatrixInverse(nullptr, DirectX::XMLoadFloat4x4(&matrices[i])));
            result.push_back({inverse, *p.boxCollider});
        }
        return result;
    }
    bool Blocked(const std::array<float, 3>& point, const std::vector<Obstacle>& obstacles, float radius)
    {
        for (const auto& obstacle : obstacles)
        {
            auto bottom = point, top = point;
            bottom[1] += .05f;
            top[1] += 1.8f;
            const auto low = Point(bottom, DirectX::XMLoadFloat4x4(&obstacle.inverse));
            const auto high = Point(top, DirectX::XMLoadFloat4x4(&obstacle.inverse));
            const auto& c = obstacle.box;
            bool overlap = true;
            for (size_t axis = 0; axis < 3; ++axis)
            {
                // Project the world-space agent cylinder into each box-local axis.
                const float extent = radius * std::hypot(obstacle.inverse.m[0][axis], obstacle.inverse.m[2][axis]);
                overlap = overlap && std::max(low[axis], high[axis]) + extent >= c.center[axis] - c.size[axis] * .5f &&
                          std::min(low[axis], high[axis]) - extent < c.center[axis] + c.size[axis] * .5f;
            }
            if (overlap)
                return true;
        }
        return false;
    }
    float SurfaceSlope(float dx, float dz, DirectX::FXMMATRIX transform)
    {
        const auto x = DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(1, dx, 0, 0), transform);
        const auto z = DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(0, dz, 1, 0), transform);
        DirectX::XMFLOAT3 normal;
        DirectX::XMStoreFloat3(&normal, DirectX::XMVector3Cross(z, x));
        return std::atan2(std::hypot(normal.x, normal.z), std::abs(normal.y)) * 180 / DirectX::XM_PI;
    }
    using Displacements = std::map<std::string, std::array<float, 3>>;
    std::array<float, 3> ParentDisplacement(std::string id, const std::map<std::string, std::string>& parents,
                                            Displacements& moves)
    {
        // Navigation changes translation only; non-agent descendants inherit their nearest moving ancestor's world delta.
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
    std::vector<size_t> Neighbors(const NavMeshComponent& mesh, size_t cell)
    {
        const int x = static_cast<int>(cell % mesh.columns), z = static_cast<int>(cell / mesh.columns);
        std::vector<size_t> result;
        for (const auto& offset : std::array<std::array<int, 2>, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}})
        {
            const int nx = x + offset[0], nz = z + offset[1];
            if (nx < 0 || nz < 0 || nx >= static_cast<int>(mesh.columns) || nz >= static_cast<int>(mesh.rows))
                continue;
            const size_t next = static_cast<size_t>(nz) * mesh.columns + nx;
            if (mesh.walkable[next])
                result.push_back(next);
        }
        return result;
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
    std::vector<size_t> Search(const NavMeshComponent& mesh, size_t start, size_t finish)
    {
        if (start == SIZE_MAX || finish == SIZE_MAX || !mesh.walkable.at(start) || !mesh.walkable.at(finish))
            return {};
        const size_t count = mesh.walkable.size();
        std::vector<size_t> parent(count, SIZE_MAX);
        std::vector<float> cost(count, std::numeric_limits<float>::infinity());
        using Entry = std::pair<float, size_t>;
        std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
        const auto heuristic = [&](size_t cell)
        {
            return static_cast<float>(
                std::abs(static_cast<int>(cell % mesh.columns) - static_cast<int>(finish % mesh.columns)) +
                std::abs(static_cast<int>(cell / mesh.columns) - static_cast<int>(finish / mesh.columns)));
        };
        cost[start] = 0;
        open.push({heuristic(start), start});
        while (!open.empty())
        {
            const auto [priority, cell] = open.top();
            open.pop();
            if (priority > cost[cell] + heuristic(cell) + .0001f)
                continue;
            if (cell == finish)
                break;
            for (const auto next : Neighbors(mesh, cell))
            {
                const float step = 1 + std::abs(mesh.heights[next] - mesh.heights[cell]) / mesh.cellSize;
                if (cost[next] <= cost[cell] + step)
                    continue;
                cost[next] = cost[cell] + step;
                parent[next] = cell;
                open.push({cost[next] + heuristic(next), next});
            }
        }
        if (!std::isfinite(cost[finish]))
            return {};
        std::vector<size_t> result;
        for (size_t cell = finish;; cell = parent[cell])
        {
            result.push_back(cell);
            if (cell == start)
                break;
        }
        std::reverse(result.begin(), result.end());
        return result;
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
                candidate.columns = owner.terrain->columns - 1;
                candidate.rows = owner.terrain->rows - 1;
                candidate.cellSize = owner.terrain->cellSize;
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
            const auto obstacles = Obstacles(layout, matrices, owner.id);
            for (unsigned int z = 0; z < candidate.rows; ++z)
                for (unsigned int x = 0; x < candidate.columns; ++x)
                {
                    const float px = (x + .5f) * candidate.cellSize, pz = (z + .5f) * candidate.cellSize;
                    float height = 0, dx = 0, dz = 0;
                    if (owner.terrain)
                    {
                        const auto& terrain = *owner.terrain;
                        height = TerrainHeight(terrain, px, pz);
                        dx = (TerrainHeight(terrain, px + candidate.cellSize * .5f, pz) -
                              TerrainHeight(terrain, px - candidate.cellSize * .5f, pz)) /
                             candidate.cellSize;
                        dz = (TerrainHeight(terrain, px, pz + candidate.cellSize * .5f) -
                              TerrainHeight(terrain, px, pz - candidate.cellSize * .5f)) /
                             candidate.cellSize;
                    }
                    const size_t cell = z * candidate.columns + x;
                    candidate.heights[cell] = height;
                    const float slope = SurfaceSlope(dx, dz, DirectX::XMLoadFloat4x4(&transform));
                    candidate.walkable[cell] = slope <= candidate.maxSlope &&
                                               !Blocked(Point({px, height, pz}, DirectX::XMLoadFloat4x4(&transform)),
                                                        obstacles, candidate.agentRadius);
                }
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
        const auto cells = Search(mesh, Cell(mesh, localStart), Cell(mesh, localEnd));
        std::vector<std::array<float, 3>> result;
        std::transform(cells.begin(), cells.end(), std::back_inserter(result),
                       [&](const auto cell)
                       {
                           return Point({(static_cast<float>(cell % mesh.columns) + .5f) * mesh.cellSize,
                                         mesh.heights[cell],
                                         (static_cast<float>(cell / mesh.columns) + .5f) * mesh.cellSize},
                                        world);
                       });
        if (!result.empty())
            result.back() = Point({localEnd[0], mesh.heights[cells.back()], localEnd[2]}, world);
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
            // All agents plan from the same pre-step hierarchy and NavMesh, regardless of object order.
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
