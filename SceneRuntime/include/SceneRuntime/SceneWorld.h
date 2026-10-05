#pragma once
#include <SceneRuntime/SceneLayout.h>
#include <Engine/Graphics/Models/ModelManager.h>
#include <Engine/Graphics/Models/Object3D.h>
#include <optional>

namespace Engine { class DirectX12Renderer; }
namespace SceneRuntime
{
    // 描画と配置データだけを共有します。ゲーム入力・編集UI・演出を持ちません。
    class SceneWorld final
    {
    public:
        bool Initialize(Engine::DirectX12Renderer& renderer, const std::filesystem::path& assetsRoot,
            const std::filesystem::path& layoutPath, const std::filesystem::path& shaderPath, std::string* error = nullptr);
        // 描画の外でGPU完了を待ってから呼びます。失敗時は元の街を維持します。
        bool Reload(const std::filesystem::path& assetsRoot, const std::filesystem::path& layoutPath, std::string& error);
        void Draw(ID3D12GraphicsCommandList* commands, const Engine::Camera& camera,
            const Engine::DirectionalLight& light) const;
        const SceneLayout& Layout() const { return layout_; }
        // 配置と描画用の変換を同時に更新します。失敗した場合は直前の状態を維持します。
        bool SetTransform(std::string_view id, const std::array<float, 3>& position,
            const std::array<float, 3>& rotation, const std::array<float, 3>& scale);
        // GPU完了を待った後、描画の外で呼びます。
        bool AddObject(ScenePlacement placement, const std::filesystem::path& assetsRoot,
            std::string& createdId, std::string& error);
        bool DuplicateObject(std::string_view id, const std::array<float, 3>& offset,
            std::string& createdId, std::string& error);
        bool RemoveObject(std::string_view id);
        std::optional<std::string> PickRay(const std::array<float, 3>& origin,
            const std::array<float, 3>& direction, float maxDistance = 220.0f) const;
        bool WorldBounds(std::string_view id, std::array<std::array<float, 3>, 8>& corners) const;
    private:
        std::string NewId();
        size_t nextObjectId_ = 1;
        void Append(ScenePlacement placement, Engine::Object3D object);
        bool modelsReady_ = false;
        SceneLayout layout_;
        Engine::ModelManager models_;
        std::vector<Engine::Object3D> objects_;
    };
}
