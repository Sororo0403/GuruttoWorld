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
        bool Initialize(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& assetsRoot,
            const std::filesystem::path& layoutPath, const std::filesystem::path& shaderPath, std::string* error = nullptr);
        bool Initialize(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& assetsRoot,
            SceneLayout layout, const std::filesystem::path& shaderPath, std::string* error = nullptr);
        // 描画の外でGPU完了を待ってから呼びます。失敗時は元の街を維持します。
        bool Reload(const std::filesystem::path& assetsRoot, const std::filesystem::path& layoutPath, std::string& error);
        // Rebuild resources from the current unsaved layout; failure preserves every live resource.
        bool ReloadAssets(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& assetsRoot,
            const std::filesystem::path& shaderPath, std::string& error);
        bool ReplaceLayout(SceneLayout layout, const std::filesystem::path& assetsRoot, std::string& error);
        void Draw(ID3D12GraphicsCommandList* commands, const Engine::Camera& camera,
            const Engine::DirectionalLight& light) const;
        const SceneLayout& Layout() const { return layout_; }
        // 配置と描画用の変換を同時に更新します。失敗した場合は直前の状態を維持します。
        // Local values are always relative to the parent.
        bool SetLocalTransform(std::string_view id, const std::array<float, 3>& position,
            const std::array<float, 3>& rotation, const std::array<float, 3>& scale);
        // Set a world pose; convert to the stored SRT and reject unrepresentable shear atomically.
        bool SetWorldTransform(std::string_view id, const DirectX::XMFLOAT4X4& matrix);
        bool SetWorldTransform(std::string_view id, const std::array<float,3>& position,
            const std::array<float,3>& rotation, const std::array<float,3>& scale);
        // Translate all IDs atomically; delta is always in world coordinates.
        // Selected descendants follow selected ancestors once.
        bool TranslateObjectsWorld(const std::vector<std::string>& ids, const std::array<float,3>& delta);
        // Rotate around a world pivot once per selected branch; reject unrepresentable local shear atomically.
        bool RotateObjectsWorld(const std::vector<std::string>& ids, const std::array<float,3>& pivot,
            const DirectX::XMFLOAT4X4& rotation);
        // Scale around a pivot along orthonormal world axes with positive factors; selected descendants follow once.
        bool ScaleObjectsWorld(const std::vector<std::string>& ids, const std::array<float,3>& pivot,
            const DirectX::XMFLOAT4X4& axes, const std::array<float,3>& factors);
        bool SetParent(std::string_view id, std::string parentId, std::string& error);
        // 表示名だけを変更します。ID・描画リソースは維持します。
        // Component changes are transactional. Call outside Render after GPU idle.
        bool SetComponents(std::string_view id, const ScenePlacement& settings,
            const std::filesystem::path& assetsRoot, std::string& error);
        // Runtime-only update; elapsed seconds rotates enabled Rotators in local coordinates.
        bool UpdateComponents(double seconds);
        bool RenameObject(std::string_view id, std::string name);
        // GPU完了を待った後、描画の外で呼びます。
        bool AddObject(ScenePlacement placement, const std::filesystem::path& assetsRoot,
            std::string& createdId, std::string& error);
        bool DuplicateObject(std::string_view id, const std::array<float, 3>& offset,
            std::string& createdId, std::string& error);
        // Copy selected objects once, remap selected parents, and apply one world offset per copied branch.
        bool DuplicateObjects(const std::vector<std::string>& ids, const std::array<float, 3>& offset,
            std::vector<std::string>& createdIds, std::string& error);
        // Remove exactly these IDs atomically. Surviving children of removed objects become roots.
        bool RemoveObjects(const std::vector<std::string>& ids, std::string& error);
        std::optional<std::string> PickRay(const std::array<float, 3>& origin,
            const std::array<float, 3>& direction, float maxDistance = 220.0f) const;
        // Rotation-only frame: combine ancestor rotations without scale, reflection or shear.
        bool WorldRotation(std::string_view id, DirectX::XMFLOAT4X4& matrix) const;
        bool WorldMatrix(std::string_view id, DirectX::XMFLOAT4X4& matrix) const;
        // Convert a world matrix to the parent-relative local SRT, preserving output on failure.
        bool LocalTransformFromWorld(std::string_view id, const DirectX::XMFLOAT4X4& world, ScenePlacement& placement) const;
        bool WorldBounds(std::string_view id, std::array<std::array<float, 3>, 8>& corners) const;
    private:
        bool InitializeModels(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& shaderPath,
            std::string* error);
        bool ReparentPlacement(ScenePlacement& placement, std::string parentId, std::string& error) const;
        bool TranslatePlacement(ScenePlacement& placement, const std::array<float,3>& delta) const;
        static bool PrepareTransforms(const SceneLayout& layout, std::vector<Engine::Object3D>& objects, std::string& error);
        bool SetPlacementTransform(std::string_view id, const ScenePlacement& placement);
        bool CommitTransforms(SceneLayout candidate);
        bool TransformObjectsWorld(const std::vector<std::string>& ids, const DirectX::XMFLOAT4X4& delta);
        std::string NewId(size_t& nextCounter) const;
        size_t nextObjectId_ = 1;
        void Append(ScenePlacement placement, Engine::Object3D object);
        bool modelsReady_ = false;
        SceneLayout layout_;
        Engine::ModelManager models_;
        std::vector<Engine::Object3D> objects_;
    };
}
