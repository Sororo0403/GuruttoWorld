#pragma once
#include <Engine/Core/Validation.h>
#include <SceneRuntime/Rendering/SceneView.h>
#include <Engine/Graphics/Models/ModelLoader.h>
#include <fstream>
#include <cstring>

namespace SceneRuntime
{
    class LightmapBaker final
    {
    public:
        /// <summary>元UV上へ静的な平行・局所光源と実形状の遮蔽をCPUでベイクします。</summary>
        static std::vector<unsigned char> Bake(const SceneWorld& world, const std::filesystem::path& root,
                                               const std::string& id, UINT resolution = 128);
        /// <summary>線形照度をsRGBの32bit BMPとして安全なAssets/Texturesパスへ保存します。</summary>
        static void Save(const std::filesystem::path& root, const std::filesystem::path& path, UINT resolution,
                         const std::vector<unsigned char>& pixels);

    private:
        /// <summary>光源へ向かう実形状のレイで照明の可視性を確認します。</summary>
        static bool Visible(const SceneWorld& world, const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& normal,
                            const DirectX::XMFLOAT3& direction, float distance);
        /// <summary>環境光と遮蔽された平行・点・スポット光の照度を評価します。</summary>
        static std::array<float, 3> Irradiance(const SceneWorld& world, const DirectX::XMFLOAT3& position,
                                               const DirectX::XMFLOAT3& normal,
                                               const Engine::DirectionalLight& lighting);
        /// <summary>UVの三角形を画素へ走査し、補間したワールド位置と法線を照明へ渡します。</summary>
        static bool Rasterize(const SceneWorld& world, const std::array<Engine::MeshVertex, 3>& triangle,
                              const DirectX::XMFLOAT4X4& transform, const Engine::DirectionalLight& lighting,
                              UINT resolution, std::vector<unsigned char>& pixels);
        /// <summary>重心座標の位置・法線をワールドへ変換し、一つのUV画素の照度を評価します。</summary>
        static std::array<float, 3> Shade(const SceneWorld& world, const std::array<Engine::MeshVertex, 3>& triangle,
                                          const DirectX::XMMATRIX& matrix, const DirectX::XMMATRIX& normalMatrix,
                                          const Engine::DirectionalLight& lighting,
                                          const std::array<float, 3>& weights);
        /// <summary>未使用UV境界へ隣接色を広げ、補間時の黒い継ぎ目を減らします。</summary>
        static void Dilate(UINT resolution, std::vector<unsigned char>& pixels);
    };
}
