#include <Engine/Graphics/Renderers/SphereRenderer.h>
#include <Engine/Core/Log.h>

#include <cmath>
#include <numbers>

namespace Engine
{
    bool SphereRenderer::Initialize(ID3D12Device* device, ID3D12CommandQueue* queue,
        const std::filesystem::path& texturePath, const std::filesystem::path& shaderPath)
    {
        MeshData mesh;
        GenerateMesh(mesh.vertices, mesh.indices);
        mesh.texturePath = texturePath;
        if (!renderer_.Initialize(device, queue, mesh, shaderPath))
        {
            return false;
        }
        Log::Info("Sphere renderer initialized: 3968 triangles.");
        return true;
    }

    void SphereRenderer::GenerateMesh(std::vector<MeshVertex>& vertices, std::vector<std::uint32_t>& indices)
    {
        constexpr UINT LatitudeCount = 32;
        constexpr UINT LongitudeCount = 64;
        vertices.reserve((LatitudeCount + 1) * (LongitudeCount + 1));
        indices.reserve(6 * LongitudeCount * (LatitudeCount - 1));
        for (UINT latitude = 0; latitude <= LatitudeCount; ++latitude)
        {
            const float v = static_cast<float>(latitude) / LatitudeCount;
            const float theta = v * std::numbers::pi_v<float>;
            // 極と継ぎ目を正確に一致させ、浮動小数点の誤差による隙間を防ぎます。
            const float radius = latitude == 0 || latitude == LatitudeCount ? 0.0f : std::sin(theta);
            for (UINT longitude = 0; longitude <= LongitudeCount; ++longitude)
            {
                const float u = static_cast<float>(longitude) / LongitudeCount;
                const float phi = longitude == LongitudeCount ? 0.0f : u * 2.0f * std::numbers::pi_v<float>;
                const std::array<float, 3> position{ radius * std::cos(phi), std::cos(theta), radius * std::sin(phi) };
                vertices.push_back({ position, position, { u, v } });
            }
        }
        for (UINT latitude = 0; latitude < LatitudeCount; ++latitude)
        {
            for (UINT longitude = 0; longitude < LongitudeCount; ++longitude)
            {
                const UINT a = latitude * (LongitudeCount + 1) + longitude;
                const UINT b = a + LongitudeCount + 1;
                if (latitude != 0)
                {
                    indices.insert(indices.end(), { a, a + 1, b });
                }
                if (latitude + 1 != LatitudeCount)
                {
                    indices.insert(indices.end(), { a + 1, b + 1, b });
                }
            }
        }
    }

    void SphereRenderer::Draw(ID3D12GraphicsCommandList* commands, const DirectX::XMFLOAT4X4& world,
        const DirectX::XMFLOAT4X4& viewProjection, const DirectionalLight& light,
        const std::array<float, 3>& cameraPosition, const UvTransform& uvTransform) const
    {
        renderer_.Draw(commands, world, viewProjection, light, cameraPosition, uvTransform);
    }
}
