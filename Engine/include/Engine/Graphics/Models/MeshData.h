#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace Engine
{
    struct MeshVertex
    {
        std::array<float, 3> position{};
        std::array<float, 3> normal{};
        std::array<float, 2> uv{};
        std::array<float, 4> color{ 1.0f, 1.0f, 1.0f, 1.0f };
    };

    struct MeshData
    {
        std::vector<MeshVertex> vertices;
        std::vector<std::uint32_t> indices;
        // 空の場合は白テクスチャを使用し、頂点色だけを反映します。
        std::filesystem::path texturePath;
    };
}
