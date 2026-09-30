#pragma once
#include <Engine/Graphics/Camera.h>
#include <Engine/Graphics/Models/ModelManager.h>
#include <Engine/Graphics/Models/Object3D.h>
#include <vector>

namespace Engine { class DirectX12Renderer; }

namespace App
{
    class TitleEnvironment final
    {
    public:
        /// <summary>
        /// CC0 モデルを共有して街並みを配置し、固定カメラと光を設定します。
        /// </summary>
        bool Initialize(Engine::DirectX12Renderer& renderer, const std::filesystem::path& root);
        /// <summary>
        /// 画面の縦横比を反映し、道路・橋・建物を深度付きで描画します。
        /// </summary>
        void Draw(ID3D12GraphicsCommandList* commands, float aspectRatio);
    private:
        /// <summary>
        /// 歩道・屋上・橋の植生と街路の看板を CC0 モデルで配置します。
        /// </summary>
        bool AddGreeneryAndSigns(const std::filesystem::path& root);
        /// <summary>
        /// 共有モデルに個別の変換を設定して街の配置へ追加します。
        /// </summary>
        bool AddObject(const std::filesystem::path& path, const std::array<float, 3>& position,
            float yaw, const std::array<float, 3>& scale);
        Engine::ModelManager models_;
        std::vector<Engine::Object3D> objects_;
        Engine::Camera camera_;
        Engine::DirectionalLight light_;
    };
}
