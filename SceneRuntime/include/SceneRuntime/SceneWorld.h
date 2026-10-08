#pragma once
#include <SceneRuntime/SceneLayout.h>
#include <Engine/Graphics/Models/ModelManager.h>
#include <Engine/Graphics/Models/Object3D.h>
#include <optional>
#include <SceneRuntime/ScenePhysics.h>
#include <SceneRuntime/ScriptRuntime.h>
#include <SceneRuntime/PhysicsWorld.h>

namespace Engine { class DirectX12Renderer; }
namespace SceneRuntime { struct UiState; }
namespace SceneRuntime
{
    // 描画と配置データだけを共有します。ゲーム入力・編集UI・演出を持ちません。
    struct MeshTelemetry { size_t draws=0,triangles=0; };
    class SceneWorld final
    {
    public:
        ~SceneWorld() { scripts_.Stop(layout_); }
        bool Initialize(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& assetsRoot,
            const std::filesystem::path& layoutPath, const std::filesystem::path& shaderPath, std::string* error = nullptr);
        bool Initialize(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& assetsRoot,
            SceneLayout layout, const std::filesystem::path& shaderPath, std::string* error = nullptr);
        // 描画の外でGPU完了を待ってから呼びます。失敗時は元の街を維持します。
        bool Reload(const std::filesystem::path& assetsRoot, const std::filesystem::path& layoutPath, std::string& error);
        // Rebuild resources from the current unsaved layout; failure preserves every live resource.
        bool ReloadAssets(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& assetsRoot,
            const std::filesystem::path& shaderPath, std::string& error);
        bool ReplaceLayout(SceneLayout layout, const std::filesystem::path& assetsRoot, std::string& error,bool preserveExecution=false);
        void Draw(ID3D12GraphicsCommandList* commands, const Engine::Camera& camera,
            const Engine::DirectionalLight& light, const UiState* state=nullptr) const;
        const MeshTelemetry& Telemetry() const { return meshTelemetry_; }
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
        bool SetSettings(const SceneSettings& settings, std::string& error);
        bool SetComponents(std::string_view id, const ScenePlacement& settings,
            const std::filesystem::path& assetsRoot, std::string& error);
        // Runtime-only update; elapsed seconds rotates enabled Rotators in local coordinates.
        bool UpdateComponents(double seconds);
        bool QueueScriptEvent(ScriptEvent event,std::string& error);
        void SetRuntimePreparation(std::function<bool(const SceneLayout&,std::string&)> prepare) { prepareRuntime_=std::move(prepare); }
        std::string AnimatorStateName(const std::string& id) const { const auto found=animatorStates_.find(id); return found==animatorStates_.end() ? std::string{} : found->second.current; }
        /// <summary>現在のクリップ重み・同期位相・骨格姿勢を取得します。変更操作後に再取得してください。</summary>
        const AnimatorState* AnimatorStatus(const std::string& id) const { const auto found=animatorStates_.find(id); return found==animatorStates_.end() ? nullptr : &found->second; }
        /// <summary>個体のAnimatorパラメーターを実行中だけ上書きします。保存したComponentには反映しません。</summary>
        bool SetAnimatorParameter(const std::string& id,const std::string& name,float value);
        /// <summary>個体のパラメーター上書きを解除し、入力または保存値を使用します。</summary>
        bool ClearAnimatorParameter(const std::string& id,const std::string& name);
        void SetInputActions(std::map<std::string,float> values,std::map<std::string,bool> pressed) { inputValues_=std::move(values); inputPressed_=std::move(pressed); }
        // Normalized input moves controllers on their parent-local XZ plane.
        bool MovePlayers(double seconds, float horizontal, float vertical, bool jump=false);
        bool AddImpulse(const std::string& id,const std::array<float,3>& impulse);
        std::optional<PhysicsRayHit> PhysicsRaycast(const std::array<float,3>& origin,const std::array<float,3>& direction,float distance,
            unsigned int mask=0xffffffffu,bool triggers=false,const std::string& ignore={}) const { return physicsWorld_.Raycast(origin,direction,distance,mask,triggers,ignore); }
        /// <summary>保存済みトラックを時計から評価し、3D配置へ一括反映します。</summary>
        bool Animate(const std::map<std::string, float>& clocks);
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
        struct PreparedLayout
        {
            SceneLayout layout;
            std::filesystem::path assetsRoot;
            std::vector<Engine::Object3D> objects;
            std::map<std::string,std::shared_ptr<Engine::ModelRenderer>> animated;
            std::map<std::string,AnimatorState> states;
        };
        struct AnimatorFrame
        {
            std::string id;
            std::shared_ptr<Engine::ModelRenderer> model;
            AnimatorState state;
            Engine::ModelRenderer::PreparedPose pose;
            bool applyPose=false;
        };
        /// <summary>配置・モデル・初期Animatorを候補へ構築し、現在のシーンを保持します。</summary>
        bool PrepareLayout(SceneLayout layout,const std::filesystem::path& assetsRoot,std::string& error,bool preserveExecution,PreparedLayout& result);
        /// <summary>準備した配置とリソースをシーンへ反映します。</summary>
        void CommitLayout(PreparedLayout&& prepared,bool preserveExecution);
        /// <summary>全個体の姿勢とイベントを検証し、Scriptの候補キューへ予約します。</summary>
        bool PrepareAnimators(const SceneLayout& layout,double seconds,const std::map<std::string,std::shared_ptr<Engine::ModelRenderer>>& models,
            const std::map<std::string,AnimatorState>& states,ScriptRuntime& scripts,std::vector<AnimatorFrame>& result,std::string& error) const;
        mutable MeshTelemetry meshTelemetry_;
        std::filesystem::path assetsRoot_;
        std::function<bool(const SceneLayout&,std::string&)> prepareRuntime_;
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
        std::map<std::string,float> inputValues_;
        std::map<std::string,bool> inputPressed_;
        std::map<std::string,std::shared_ptr<Engine::ModelRenderer>> animatedModels_;
        std::map<std::string,AnimatorState> animatorStates_;
        ScriptRuntime scripts_;
        ScenePhysics::States physics_;
        PhysicsWorld physicsWorld_;
        Microsoft::WRL::ComPtr<ID3D12Device> materialDevice_;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> materialQueue_;
        bool modelsReady_ = false;
        mutable Engine::ShadowMap shadow_;
        SceneLayout layout_;
        Engine::ModelManager models_;
        std::vector<Engine::Object3D> objects_;
    };
}
