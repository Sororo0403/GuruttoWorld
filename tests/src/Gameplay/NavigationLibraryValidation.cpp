#include <SceneRuntime/Gameplay/Navigation.h>
#include <SceneRuntime/Scene/SceneTransforms.h>
#include "../Physics/NavigationTransformValidation.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
    using namespace SceneRuntime;
    using Point = std::array<float, 3>;

    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    SceneLayout Grid(unsigned int size = 6)
    {
        SceneLayout layout;
        ScenePlacement mesh;
        mesh.id = "mesh";
        mesh.navMesh.emplace();
        mesh.navMesh->columns = size;
        mesh.navMesh->rows = size;
        mesh.navMesh->heights.assign(size * size, 0);
        mesh.navMesh->walkable.assign(size * size, true);
        mesh.navMesh->baked = true;
        layout.objects.push_back(mesh);
        return layout;
    }

    void SafeSegments(const NavMeshComponent& mesh, const std::vector<Point>& path)
    {
        Require(path.size() >= 2, "Detour returns a complete path");
        for (size_t edge = 1; edge < path.size(); ++edge)
        {
            for (int sample = 1; sample < 100; ++sample)
            {
                const float factor = sample / 100.0f;
                const float x = std::lerp(path[edge - 1][0], path[edge][0], factor);
                const float z = std::lerp(path[edge - 1][2], path[edge][2], factor);
                const float gridX = x / mesh.cellSize, gridZ = z / mesh.cellSize;
                // セル境界上は両側の共有辺なので、障害物セルの内部だけを拒否します。
                if (std::abs(gridX - std::round(gridX)) < .0001f || std::abs(gridZ - std::round(gridZ)) < .0001f)
                {
                    continue;
                }
                const auto column = static_cast<unsigned int>(gridX), row = static_cast<unsigned int>(gridZ);
                if (!(column < mesh.columns && row < mesh.rows && mesh.walkable[row * mesh.columns + column]))
                {
                    std::cerr << "Unsafe point: " << x << "," << z << "\n";
                    for (const auto& point : path)
                    {
                        std::cerr << "Path: " << point[0] << "," << point[1] << "," << point[2] << "\n";
                    }
                    for (unsigned int rowIndex = 0; rowIndex < mesh.rows; ++rowIndex)
                    {
                        for (unsigned int columnIndex = 0; columnIndex < mesh.columns; ++columnIndex)
                        {
                            std::cerr << (mesh.walkable[rowIndex * mesh.columns + columnIndex] ? '.' : '#');
                        }
                        std::cerr << "\n";
                    }
                    Require(false, "every path segment avoids blocked cell interiors");
                }
            }
        }
    }

    void ManualGrid()
    {
        auto layout = Grid();
        auto& mesh = *layout.objects[0].navMesh;
        for (unsigned int row = 0; row < mesh.rows; ++row)
        {
            mesh.walkable[row * mesh.columns + 2] = row == 4;
        }
        const Point start{.5f, 0, 1.5f}, end{5.5f, 0, 1.5f};
        const auto path = Navigation::Path(layout, "mesh", start, end);
        SafeSegments(mesh, path);
        Require(path.front() == start && path.back() == end, "path preserves exact projected endpoints");
        Require(std::any_of(path.begin(), path.end(),
                            [](const Point& point)
                            {
                                return point[2] >= 4;
                            }),
                "path takes the only open passage");
        mesh.walkable[4 * mesh.columns + 2] = false;
        Require(Navigation::Path(layout, "mesh", start, end).empty(), "manual edit invalidates cached connectivity");
        mesh.walkable[4 * mesh.columns + 2] = true;
        Require(!Navigation::Path(layout, "mesh", start, end).empty(), "restored manual passage is reusable");
        Require(Navigation::Path(layout, "mesh", {2.5f, 0, 1.5f}, end).empty(),
                "blocked endpoint is not snapped across a wall");
        auto diagonal = Grid(2);
        diagonal.objects[0].navMesh->walkable = {true, false, false, true};
        Require(Navigation::Path(diagonal, "mesh", {.5f, 0, .5f}, {1.5f, 0, 1.5f}).empty(),
                "disconnected cells touching at one corner have no route");
    }

    void BakedObstacle()
    {
        auto layout = Grid(4);
        ScenePlacement obstacle;
        obstacle.id = "wall";
        obstacle.position = {1.5f, .5f, 1.5f};
        obstacle.boxCollider.emplace();
        obstacle.boxCollider->size = {.8f, 1, .8f};
        layout.objects.push_back(obstacle);
        std::string error;
        Require(Navigation::Bake(layout, layout.objects[0], error), error.c_str());
        const auto& mesh = *layout.objects[0].navMesh;
        Require(!mesh.walkable[5], "Recast excludes obstacle cell");
        SafeSegments(mesh, Navigation::Path(layout, "mesh", {.5f, 0, 1.5f}, {3.5f, 0, 1.5f}));

        ScenePlacement parent;
        parent.id = "parent";
        parent.position = {.5f, 0, 1.5f};
        ScenePlacement agent;
        agent.id = "agent";
        agent.parentId = "parent";
        agent.navAgent.emplace();
        agent.navAgent->mesh = "mesh";
        agent.navAgent->destination = {3.5f, 20, 1.5f};
        agent.navAgent->speed = 2;
        layout.objects.push_back(parent);
        layout.objects.push_back(agent);
        auto thirty = layout, oneTwenty = layout;
        for (int step = 0; step < 120; ++step)
        {
            Require(Navigation::Advance(thirty, 1.0 / 30, error), error.c_str());
        }
        for (int step = 0; step < 480; ++step)
        {
            Require(Navigation::Advance(oneTwenty, 1.0 / 120, error), error.c_str());
        }
        for (size_t axis = 0; axis < 3; ++axis)
        {
            Require(std::abs(thirty.objects.back().position[axis] - oneTwenty.objects.back().position[axis]) < .02f,
                    "Detour path following is equivalent across frame rates");
        }
        Require(!thirty.objects.back().navAgent->moving && std::abs(thirty.objects.back().position[0] - 3) < .1f &&
                    std::abs(thirty.objects.back().position[1]) < .001f,
                "parent-local agent arrives at projected surface destination");
    }

    void SavedHeights()
    {
        auto layout = Grid();
        auto& mesh = *layout.objects[0].navMesh;
        for (unsigned int row = 0; row < mesh.rows; ++row)
        {
            mesh.heights[row * mesh.columns + 2] = 3;
        }
        const auto raised = Navigation::Path(layout, "mesh", {.5f, 0, 1.5f}, {5.5f, 20, 1.5f});
        SafeSegments(mesh, raised);
        Require(std::any_of(raised.begin(), raised.end(),
                            [](const Point& point)
                            {
                                return point[1] > 2.9f;
                            }),
                "straight path retains intermediate saved cell heights");
        Require(raised.front()[1] == 0 && raised.back()[1] == 0,
                "saved grid projects endpoint heights independently of requested altitude");
        std::fill(mesh.heights.begin(), mesh.heights.end(), 50000.0f);
        const auto elevated = Navigation::Path(layout, "mesh", {.5f, 0, 1.5f}, {5.5f, -20, 1.5f});
        Require(!elevated.empty() && std::all_of(elevated.begin(), elevated.end(),
                                                 [](const Point& point)
                                                 {
                                                     return point[1] == 50000;
                                                 }),
                "saved high-altitude grid remains traversable without native voxel height overflow");
        auto single = Grid(1);
        SafeSegments(*single.objects[0].navMesh, Navigation::Path(single, "mesh", {.2f, 0, .2f}, {.8f, 0, .8f}));
    }

    void Headroom(float height, bool expected)
    {
        auto layout = Grid(4);
        ScenePlacement roof;
        roof.id = "roof";
        roof.position = {1.5f, height, 1.5f};
        roof.boxCollider.emplace();
        roof.boxCollider->size = {.8f, .2f, .8f};
        layout.objects.push_back(roof);
        std::string error;
        Require(Navigation::Bake(layout, layout.objects[0], error), error.c_str());
        Require(layout.objects[0].navMesh->walkable[5] == expected,
                "Recast headroom filter rejects only ceilings below agent height");
    }

    void SlopeBoundary()
    {
        auto flat = Grid(1);
        flat.objects[0].navMesh->maxSlope = 0;
        std::string error;
        Require(Navigation::Bake(flat, flat.objects[0], error) && flat.objects[0].navMesh->walkable[0],
                "zero slope limit still includes an exactly horizontal surface");
        flat.objects[0].terrain.emplace();
        flat.objects[0].terrain->columns = 2;
        flat.objects[0].terrain->rows = 2;
        flat.objects[0].terrain->heights = {0, 1, 0, 1};
        flat.objects[0].navMesh->maxSlope = 45;
        Require(Navigation::Bake(flat, flat.objects[0], error) && flat.objects[0].navMesh->walkable[0],
                "slope limit includes its boundary within float precision");
        auto invalid = Grid(1);
        invalid.objects[0].navMesh->columns = 0;
        const auto before = *invalid.objects[0].navMesh;
        Require(!Navigation::Bake(invalid, invalid.objects[0], error) && *invalid.objects[0].navMesh == before,
                "invalid bake dimensions fail without modifying saved grid");
    }

    void ObstacleScale(float scale, float center, bool expected)
    {
        auto layout = Grid();
        ScenePlacement obstacle;
        obstacle.id = "box";
        obstacle.parentId = "box-parent";
        obstacle.boxCollider.emplace();
        obstacle.boxCollider->size = {1, 2, 1};
        ScenePlacement parent;
        parent.id = "box-parent";
        parent.position = {center, 1, .5f};
        parent.scale = {scale, 1, 1};
        layout.objects.push_back(obstacle);
        layout.objects.push_back(parent);
        std::string error;
        Require(Navigation::Bake(layout, layout.objects[0], error), error.c_str());
        Require(layout.objects[0].navMesh->walkable[0] == expected,
                "Recast radius uses world units under scaled and mirrored obstacle parents");
    }

    void Hierarchy(bool reversed)
    {
        auto layout = NavigationTransformValidation::Family(reversed);
        std::string error;
        Require(Navigation::Advance(layout, 1, error), error.c_str());
        std::vector<DirectX::XMFLOAT4X4> matrices;
        Require(SceneTransforms::Resolve(layout, matrices, error), error.c_str());
        for (size_t index = 0; index < layout.objects.size(); ++index)
        {
            if (layout.objects[index].id != "mesh")
            {
                Require(std::abs(matrices[index]._41 - 1.5f) < .001f,
                        "nested agents move once independent of object order");
            }
        }
    }
}

int main()
{
    try
    {
        ManualGrid();
        SavedHeights();
        BakedObstacle();
        Headroom(1.5f, false);
        Headroom(3, true);
        SlopeBoundary();
        ObstacleScale(2, 1.8f, true);
        ObstacleScale(.5f, .9f, false);
        ObstacleScale(-2, 1.8f, true);
        NavigationTransformValidation::Slope(1, true);
        NavigationTransformValidation::Slope(2, false);
        NavigationTransformValidation::Slope(.5f, true);
        NavigationTransformValidation::Rotation();
        Hierarchy(false);
        Hierarchy(true);
        std::cout << "PASS: Recast bake, Detour segment safety, manual edits, radius, slopes and hierarchical agents\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
