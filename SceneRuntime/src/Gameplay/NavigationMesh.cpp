#include "NavigationMesh.h"
#include <Recast.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace
{
    using namespace SceneRuntime;
    using Point = std::array<float, 3>;
    using Heightfield = std::unique_ptr<rcHeightfield, decltype(&rcFreeHeightField)>;
    using CompactField = std::unique_ptr<rcCompactHeightfield, decltype(&rcFreeCompactHeightfield)>;
    constexpr float AgentHeight = 1.8f;
    constexpr float AgentClimb = .5f;
    constexpr size_t CacheEntryLimit = 16;
    constexpr size_t CacheByteLimit = 16 * 1024 * 1024;

    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    void Validate(const NavMeshComponent& mesh)
    {
        Require(mesh.columns > 0 && mesh.columns <= 128 && mesh.rows > 0 && mesh.rows <= 128 &&
                    std::isfinite(mesh.cellSize) && mesh.cellSize >= .001f && mesh.cellSize <= 1000 &&
                    std::isfinite(mesh.agentRadius) && mesh.agentRadius >= 0 && mesh.agentRadius <= 100 &&
                    std::isfinite(mesh.maxSlope) && mesh.maxSlope >= 0 && mesh.maxSlope < 90,
                "Invalid navigation grid settings");
        Require(mesh.heights.size() == static_cast<size_t>(mesh.columns) * mesh.rows &&
                    mesh.walkable.size() == mesh.heights.size(),
                "Navigation grid size mismatch");
        Require(std::all_of(mesh.heights.begin(), mesh.heights.end(),
                            [](float height)
                            {
                                return std::isfinite(height) && height >= -100000 && height <= 100000;
                            }),
                "Navigation grid contains an invalid height");
    }

    Point Transform(const Point& point, DirectX::FXMMATRIX matrix)
    {
        DirectX::XMFLOAT3 result;
        DirectX::XMStoreFloat3(
            &result, DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(point[0], point[1], point[2], 1), matrix));
        Require(std::isfinite(result.x) && std::isfinite(result.y) && std::isfinite(result.z),
                "Navigation geometry transform is outside the supported range");
        return {result.x, result.y, result.z};
    }

    float TerrainHeight(const TerrainComponent& terrain, float x, float z)
    {
        const float tx = std::clamp(x / terrain.cellSize, 0.0f, static_cast<float>(terrain.columns - 1));
        const float tz = std::clamp(z / terrain.cellSize, 0.0f, static_cast<float>(terrain.rows - 1));
        const auto left = static_cast<unsigned int>(tx), top = static_cast<unsigned int>(tz);
        const auto right = std::min(left + 1, terrain.columns - 1), bottom = std::min(top + 1, terrain.rows - 1);
        const auto height = [&](unsigned int column, unsigned int row)
        {
            return terrain.heights.at(row * terrain.columns + column);
        };
        return std::lerp(std::lerp(height(left, top), height(right, top), tx - left),
                         std::lerp(height(left, bottom), height(right, bottom), tx - left), tz - top);
    }

    struct Geometry
    {
        std::vector<float> vertices;
        std::vector<int> triangles;

        void Quad(const std::array<Point, 4>& corners, DirectX::FXMMATRIX matrix, bool upward = false)
        {
            const int base = static_cast<int>(vertices.size() / 3);
            for (const auto& corner : corners)
            {
                const auto point = Transform(corner, matrix);
                vertices.insert(vertices.end(), point.begin(), point.end());
            }
            for (const auto& indices : std::array<std::array<int, 3>, 2>{{{0, 1, 2}, {0, 2, 3}}})
            {
                auto triangle = indices;
                const auto offset = static_cast<size_t>(base) * 3;
                const float ax = vertices[offset + triangle[1] * 3] - vertices[offset + triangle[0] * 3];
                const float az = vertices[offset + triangle[1] * 3 + 2] - vertices[offset + triangle[0] * 3 + 2];
                const float bx = vertices[offset + triangle[2] * 3] - vertices[offset + triangle[0] * 3];
                const float bz = vertices[offset + triangle[2] * 3 + 2] - vertices[offset + triangle[0] * 3 + 2];
                if (upward && az * bx - ax * bz < 0)
                {
                    std::swap(triangle[1], triangle[2]);
                }
                for (const auto index : triangle)
                {
                    triangles.push_back(base + index);
                }
            }
        }

        void Box(const BoxColliderComponent& box, DirectX::FXMMATRIX matrix)
        {
            std::array<Point, 8> corners;
            for (size_t index = 0; index < corners.size(); ++index)
            {
                for (size_t axis = 0; axis < 3; ++axis)
                {
                    corners[index][axis] =
                        box.center[axis] + ((index & (size_t(1) << axis)) ? .5f : -.5f) * box.size[axis];
                }
            }
            for (const auto& face : std::array<std::array<size_t, 4>, 6>{
                     {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}}})
            {
                Quad({corners[face[0]], corners[face[1]], corners[face[2]], corners[face[3]]}, matrix);
            }
        }
    };

    CompactField Compact(rcContext& context, rcHeightfield& field, int height, int climb)
    {
        CompactField result(rcAllocCompactHeightfield(), rcFreeCompactHeightfield);
        Require(result && rcBuildCompactHeightfield(&context, height, climb, field, *result),
                "Recast compact heightfield failed");
        return result;
    }

    struct CachedMesh
    {
        NavMeshComponent source;
        std::shared_ptr<dtNavMesh> mesh;
        size_t bytes = 0;
        unsigned long long used = 0;
    };

    CachedMesh BuildMesh(const NavMeshComponent& source)
    {
        CachedMesh result;
        result.source = source;
        const float cellHeight = source.cellSize / 256;
        const Point minimum{0, -cellHeight, 0};
        const Point maximum{source.columns * source.cellSize, cellHeight * 4, source.rows * source.cellSize};
        // 一セル幅の領域でも輪郭を形成できるよう、保存セルを細かい voxel へ展開します。
        constexpr int Subdivision = 4;
        rcContext context(false);
        Heightfield field(rcAllocHeightfield(), rcFreeHeightField);
        Require(field && rcCreateHeightfield(&context, *field, static_cast<int>(source.columns) * Subdivision,
                                             static_cast<int>(source.rows) * Subdivision, minimum.data(),
                                             maximum.data(), source.cellSize / Subdivision, cellHeight),
                "Recast navigation grid allocation failed");
        for (size_t index = 0; index < source.walkable.size(); ++index)
        {
            if (!source.walkable[index])
            {
                continue;
            }
            for (int z = 0; z < Subdivision; ++z)
            {
                for (int x = 0; x < Subdivision; ++x)
                {
                    Require(rcAddSpan(&context, *field, static_cast<int>(index % source.columns) * Subdivision + x,
                                      static_cast<int>(index / source.columns) * Subdivision + z, 0, 1,
                                      RC_WALKABLE_AREA, 0),
                            "Recast navigation grid span failed");
                }
            }
        }
        // 保存形式は一セル一高さの 2.5D です。通行領域を探索し、返却時に元の高さを補間します。
        // 半径と傾斜は Bake 済みなので、ここでは通路を二重に削りません。
        auto compact = Compact(context, *field, 3, 0);
        if (compact->spanCount == 0)
        {
            return result;
        }
        Require(rcBuildRegionsMonotone(&context, *compact, 0, 0, 0), "Recast navigation regions failed");
        std::unique_ptr<rcContourSet, decltype(&rcFreeContourSet)> contours(rcAllocContourSet(), rcFreeContourSet);
        Require(contours && rcBuildContours(&context, *compact, 0, 0, *contours), "Recast navigation contours failed");
        std::unique_ptr<rcPolyMesh, decltype(&rcFreePolyMesh)> polygons(rcAllocPolyMesh(), rcFreePolyMesh);
        Require(polygons && rcBuildPolyMesh(&context, *contours, DT_VERTS_PER_POLYGON, *polygons),
                "Recast navigation polygon mesh failed");
        if (polygons->npolys == 0)
        {
            return result;
        }
        std::fill_n(polygons->flags, polygons->npolys, static_cast<unsigned short>(1));
        dtNavMeshCreateParams parameters{};
        parameters.verts = polygons->verts;
        parameters.vertCount = polygons->nverts;
        parameters.polys = polygons->polys;
        parameters.polyFlags = polygons->flags;
        parameters.polyAreas = polygons->areas;
        parameters.polyCount = polygons->npolys;
        parameters.nvp = polygons->nvp;
        std::copy_n(polygons->bmin, 3, parameters.bmin);
        std::copy_n(polygons->bmax, 3, parameters.bmax);
        parameters.cs = polygons->cs;
        parameters.ch = polygons->ch;
        parameters.walkableHeight = AgentHeight;
        parameters.walkableRadius = source.agentRadius;
        parameters.walkableClimb = AgentClimb;
        parameters.buildBvTree = true;
        unsigned char* data = nullptr;
        int bytes = 0;
        Require(dtCreateNavMeshData(&parameters, &data, &bytes), "Detour navigation mesh data failed");
        std::unique_ptr<unsigned char, decltype(&dtFree)> owner(data, dtFree);
        result.mesh = std::shared_ptr<dtNavMesh>(dtAllocNavMesh(), dtFreeNavMesh);
        Require(result.mesh && dtStatusSucceed(result.mesh->init(data, bytes, DT_TILE_FREE_DATA)),
                "Detour navigation mesh initialization failed");
        owner.release();
        result.bytes = static_cast<size_t>(bytes) + source.heights.size() * (sizeof(float) + sizeof(bool));
        return result;
    }

    std::shared_ptr<dtNavMesh> Cached(const NavMeshComponent& source)
    {
        static std::mutex mutex;
        static std::vector<CachedMesh> entries;
        static unsigned long long sequence = 0;
        const std::lock_guard lock(mutex);
        for (auto& entry : entries)
        {
            if (entry.source == source)
            {
                entry.used = ++sequence;
                return entry.mesh;
            }
        }
        auto entry = BuildMesh(source);
        const auto result = entry.mesh;
        entry.used = ++sequence;
        entries.push_back(std::move(entry));
        size_t bytes = 0;
        for (const auto& current : entries)
        {
            bytes += current.bytes;
        }
        while (entries.size() > CacheEntryLimit || bytes > CacheByteLimit)
        {
            const auto oldest = std::min_element(entries.begin(), entries.end(),
                                                 [](const auto& first, const auto& second)
                                                 {
                                                     return first.used < second.used;
                                                 });
            bytes -= oldest->bytes;
            entries.erase(oldest);
        }
        return result;
    }

    size_t Cell(const NavMeshComponent& mesh, const Point& point)
    {
        if (!std::isfinite(point[0]) || !std::isfinite(point[1]) || !std::isfinite(point[2]) || point[0] < 0 ||
            point[2] < 0 || point[0] >= mesh.columns * mesh.cellSize || point[2] >= mesh.rows * mesh.cellSize)
        {
            return SIZE_MAX;
        }
        const float gridX = point[0] / mesh.cellSize, gridZ = point[2] / mesh.cellSize;
        const auto x = static_cast<unsigned int>(gridX), z = static_cast<unsigned int>(gridZ);
        const auto index = static_cast<size_t>(z) * mesh.columns + x;
        if (mesh.walkable[index])
        {
            return index;
        }
        // Detour の折れ点はセルの共有辺上に来ます。辺の反対側にある通行可能セルも同じ面です。
        const unsigned int left = x > 0 && std::abs(gridX - x) < .0001f ? x - 1 : x;
        const unsigned int top = z > 0 && std::abs(gridZ - z) < .0001f ? z - 1 : z;
        for (const auto column : {x, left})
        {
            for (const auto row : {z, top})
            {
                const auto adjacent = static_cast<size_t>(row) * mesh.columns + column;
                if (mesh.walkable[adjacent])
                {
                    return adjacent;
                }
            }
        }
        return index;
    }
}

namespace SceneRuntime::NavigationMesh
{
    void Bake(const SceneLayout& layout, const ScenePlacement& owner, const std::vector<DirectX::XMFLOAT4X4>& matrices,
              const DirectX::XMFLOAT4X4& transform, NavMeshComponent& mesh)
    {
        Validate(mesh);
        const auto matrix = DirectX::XMLoadFloat4x4(&transform);
        Geometry geometry;
        const unsigned int columns = owner.terrain ? owner.terrain->columns - 1 : 1;
        const unsigned int rows = owner.terrain ? owner.terrain->rows - 1 : 1;
        const float size = owner.terrain ? owner.terrain->cellSize : mesh.columns * mesh.cellSize;
        const float depth = owner.terrain ? owner.terrain->cellSize : mesh.rows * mesh.cellSize;
        for (unsigned int z = 0; z < rows; ++z)
        {
            for (unsigned int x = 0; x < columns; ++x)
            {
                const auto corner = [&](unsigned int column, unsigned int row)
                {
                    const float px = column * size, pz = row * depth;
                    return Point{px, owner.terrain ? TerrainHeight(*owner.terrain, px, pz) : 0, pz};
                };
                geometry.Quad({corner(x, z), corner(x, z + 1), corner(x + 1, z + 1), corner(x + 1, z)}, matrix, true);
            }
        }
        Point minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                      std::numeric_limits<float>::max()};
        Point maximum{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
                      std::numeric_limits<float>::lowest()};
        for (size_t index = 0; index < geometry.vertices.size(); ++index)
        {
            minimum[index % 3] = std::min(minimum[index % 3], geometry.vertices[index]);
            maximum[index % 3] = std::max(maximum[index % 3], geometry.vertices[index]);
        }
        const auto groundTriangles = static_cast<int>(geometry.triangles.size() / 3);
        for (size_t index = 0; index < layout.objects.size(); ++index)
        {
            const auto& object = layout.objects[index];
            if (object.id != owner.id && !object.navAgent && object.boxCollider && object.boxCollider->enabled &&
                !object.boxCollider->isTrigger && object.boxCollider->shape == "box")
            {
                geometry.Box(*object.boxCollider, DirectX::XMLoadFloat4x4(&matrices.at(index)));
            }
        }
        const float scaleX = std::hypot(transform._11, transform._12, transform._13);
        const float scaleZ = std::hypot(transform._31, transform._32, transform._33);
        const float desired = std::min(mesh.cellSize * std::min(scaleX, scaleZ) / 8,
                                       mesh.agentRadius > 0 ? mesh.agentRadius / 8 : mesh.cellSize);
        const float cellSize =
            std::max({desired, (maximum[0] - minimum[0]) / 2046, (maximum[2] - minimum[2]) / 2046, 0.00001f});
        // compact span の空間高は 8 bit なので、頭上の空間を細分化し過ぎないようにします。
        const float cellHeight = std::max(
            {cellSize / 4, AgentHeight / 200, (maximum[1] - minimum[1] + AgentHeight + 2) / (RC_SPAN_MAX_HEIGHT - 4)});
        minimum[0] -= cellSize;
        minimum[2] -= cellSize;
        maximum[0] += cellSize;
        maximum[2] += cellSize;
        minimum[1] -= cellHeight * 2;
        maximum[1] += AgentHeight + AgentClimb + 1;
        int width = 0, height = 0;
        rcCalcGridSize(minimum.data(), maximum.data(), cellSize, &width, &height);
        rcContext context(false);
        Heightfield field(rcAllocHeightfield(), rcFreeHeightField);
        Require(field && rcCreateHeightfield(&context, *field, width, height, minimum.data(), maximum.data(), cellSize,
                                             cellHeight),
                "Recast bake heightfield allocation failed");
        std::vector<unsigned char> areas(geometry.triangles.size() / 3, RC_NULL_AREA);
        // Recast の厳密な比較でも水平面と指定角の境界を残すため、丸め誤差だけ許容します。
        const float slopeLimit = std::max(.05f, std::min(mesh.maxSlope + .0001f, 89.999f));
        rcMarkWalkableTriangles(&context, slopeLimit, geometry.vertices.data(),
                                static_cast<int>(geometry.vertices.size() / 3), geometry.triangles.data(),
                                groundTriangles, areas.data());
        const int climb = static_cast<int>(std::floor(AgentClimb / cellHeight));
        Require(rcRasterizeTriangles(&context, geometry.vertices.data(), static_cast<int>(geometry.vertices.size() / 3),
                                     geometry.triangles.data(), areas.data(), static_cast<int>(areas.size()), *field,
                                     climb),
                "Recast bake rasterization failed");
        const int clearance = std::max(3, static_cast<int>(std::ceil(AgentHeight / cellHeight)));
        rcFilterWalkableLowHeightSpans(&context, clearance, *field);
        auto compact = Compact(context, *field, clearance, climb);
        const auto radius = static_cast<int>(std::ceil(mesh.agentRadius / cellSize));
        if (radius > 0)
        {
            Require(rcErodeWalkableArea(&context, radius, *compact), "Recast agent radius erosion failed");
        }
        std::fill(mesh.walkable.begin(), mesh.walkable.end(), false);
        for (unsigned int z = 0; z < mesh.rows; ++z)
        {
            for (unsigned int x = 0; x < mesh.columns; ++x)
            {
                const size_t index = static_cast<size_t>(z) * mesh.columns + x;
                const float px = (x + .5f) * mesh.cellSize, pz = (z + .5f) * mesh.cellSize;
                mesh.heights[index] = owner.terrain ? TerrainHeight(*owner.terrain, px, pz) : 0;
                const auto world = Transform({px, mesh.heights[index], pz}, matrix);
                const int column = static_cast<int>(std::floor((world[0] - compact->bmin[0]) / compact->cs));
                const int row = static_cast<int>(std::floor((world[2] - compact->bmin[2]) / compact->cs));
                if (column < 0 || row < 0 || column >= compact->width || row >= compact->height)
                {
                    continue;
                }
                const auto& cell = compact->cells[row * compact->width + column];
                for (unsigned int span = cell.index; span < cell.index + cell.count; ++span)
                {
                    const float surface = compact->bmin[1] + compact->spans[span].y * compact->ch;
                    if (compact->areas[span] != RC_NULL_AREA &&
                        std::abs(surface - world[1]) <= std::max(cellHeight * 3, cellSize * 2))
                    {
                        mesh.walkable[index] = true;
                        break;
                    }
                }
            }
        }
    }

    std::vector<Point> Path(const NavMeshComponent& mesh, const Point& start, const Point& destination)
    {
        Validate(mesh);
        const auto first = Cell(mesh, start), last = Cell(mesh, destination);
        if (first == SIZE_MAX || last == SIZE_MAX || !mesh.walkable[first] || !mesh.walkable[last])
        {
            return {};
        }
        const auto navigation = Cached(mesh);
        if (!navigation)
        {
            return {};
        }
        std::unique_ptr<dtNavMeshQuery, decltype(&dtFreeNavMeshQuery)> query(dtAllocNavMeshQuery(), dtFreeNavMeshQuery);
        Require(query && dtStatusSucceed(query->init(navigation.get(), 65535)),
                "Detour navigation query allocation failed");
        dtQueryFilter filter;
        const Point projectedStart{start[0], mesh.heights[first], start[2]};
        const Point projectedEnd{destination[0], mesh.heights[last], destination[2]};
        const Point queryStart{start[0], 0, start[2]}, queryEnd{destination[0], 0, destination[2]};
        const Point extents{mesh.cellSize * .49f, mesh.cellSize, mesh.cellSize * .49f};
        dtPolyRef startRef = 0, endRef = 0;
        Point nearestStart{}, nearestEnd{};
        Require(dtStatusSucceed(query->findNearestPoly(queryStart.data(), extents.data(), &filter, &startRef,
                                                       nearestStart.data())) &&
                    dtStatusSucceed(
                        query->findNearestPoly(queryEnd.data(), extents.data(), &filter, &endRef, nearestEnd.data())),
                "Detour navigation endpoint query failed");
        if (!startRef || !endRef ||
            std::hypot(nearestStart[0] - start[0], nearestStart[2] - start[2]) > mesh.cellSize * .001f ||
            std::hypot(nearestEnd[0] - destination[0], nearestEnd[2] - destination[2]) > mesh.cellSize * .001f)
        {
            return {};
        }
        std::vector<dtPolyRef> corridor(mesh.walkable.size() * 4 + 1);
        int count = 0;
        const auto status = query->findPath(startRef, endRef, nearestStart.data(), nearestEnd.data(), &filter,
                                            corridor.data(), &count, static_cast<int>(corridor.size()));
        if (dtStatusFailed(status) || dtStatusDetail(status, DT_BUFFER_TOO_SMALL | DT_OUT_OF_NODES) || count == 0 ||
            corridor[count - 1] != endRef)
        {
            return {};
        }
        std::vector<Point> points(corridor.size() + 2);
        int pointCount = 0;
        const auto straight = query->findStraightPath(nearestStart.data(), nearestEnd.data(), corridor.data(), count,
                                                      points.front().data(), nullptr, nullptr, &pointCount,
                                                      static_cast<int>(points.size()));
        Require(dtStatusSucceed(straight) && !dtStatusDetail(straight, DT_BUFFER_TOO_SMALL),
                "Detour straight path failed");
        points.resize(pointCount);
        if (points.empty())
        {
            return {};
        }
        const bool flat = std::all_of(mesh.heights.begin(), mesh.heights.end(),
                                      [&](float height)
                                      {
                                          return height == mesh.heights.front();
                                      });
        if (flat)
        {
            for (auto& point : points)
            {
                point[1] = mesh.heights.front();
            }
        }
        else
        {
            const auto surface = [&](const Point& point)
            {
                const float x = std::clamp(point[0] / mesh.cellSize - .5f, 0.0f, float(mesh.columns - 1));
                const float z = std::clamp(point[2] / mesh.cellSize - .5f, 0.0f, float(mesh.rows - 1));
                const auto left = static_cast<unsigned int>(x), top = static_cast<unsigned int>(z);
                const auto right = std::min(left + 1, mesh.columns - 1), bottom = std::min(top + 1, mesh.rows - 1);
                const auto height = [&](unsigned int column, unsigned int row)
                {
                    return mesh.heights[row * mesh.columns + column];
                };
                return std::lerp(std::lerp(height(left, top), height(right, top), x - left),
                                 std::lerp(height(left, bottom), height(right, bottom), x - left), z - top);
            };
            std::vector<Point> projected{projectedStart};
            for (size_t edge = 1; edge < points.size(); ++edge)
            {
                const auto& from = points[edge - 1];
                const auto& to = points[edge];
                const int samples = std::max(
                    1, static_cast<int>(std::ceil(std::max(std::abs(to[0] - from[0]), std::abs(to[2] - from[2])) /
                                                  (mesh.cellSize * .25f))));
                Require(projected.size() + samples <= 262144, "Navigation surface path exceeds point limit");
                for (int sample = 1; sample <= samples; ++sample)
                {
                    const float factor = static_cast<float>(sample) / samples;
                    Point point{std::lerp(from[0], to[0], factor), 0, std::lerp(from[2], to[2], factor)};
                    point[1] = surface(point);
                    projected.push_back(point);
                }
            }
            points = std::move(projected);
        }
        points.front() = projectedStart;
        points.back() = projectedEnd;
        return points;
    }
}
