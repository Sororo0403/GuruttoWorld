#pragma once
#include <Engine/Graphics/Camera.h>
#include <Engine/Graphics/Models/ModelManager.h>
#include <Engine/Graphics/Models/Object3D.h>
#include <Engine/Graphics/Renderers/SpriteRenderer.h>
#include <vector>
#include <Engine/Graphics/Renderers/ParticleRenderer.h>
#include "TitleAmbientMotion.h"

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
        /// <summary>設定とフォーカス状態に合わせて背景の待機演出を更新します。</summary>
        void Update(double deltaSeconds, bool enabled, bool active, bool settingsSelected = false, bool exitSelected = false);
        /// <summary>空・街並み・奥行きのある光の粒を描画します。</summary>
        void Draw(ID3D12GraphicsCommandList* commands, unsigned int width, unsigned int height);
    private:
        bool BuildStreet(const std::filesystem::path& root);
        bool AddDistantBuildings(const std::filesystem::path& commercial);
        bool AddSidewalkGreenery(const std::filesystem::path& nature);
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
        Engine::SpriteRenderer sky_;
        Engine::ParticleRenderer motes_;
        TitleAmbientMotion motion_;
        std::vector<Engine::Object3D> objects_;
        Engine::Camera camera_;
        Engine::DirectionalLight light_;
    };
}
