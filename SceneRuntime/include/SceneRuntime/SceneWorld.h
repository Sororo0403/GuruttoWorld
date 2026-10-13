#pragma once
#include <Engine/Assets/AssetDatabase.h>
#include <Engine/Graphics/DirectX12/GpuRetirement.h>
#include <SceneRuntime/SceneLayout.h>
#include <Engine/Graphics/Models/ModelManager.h>
#include <Engine/Graphics/Models/Object3D.h>
#include <optional>
#include <unordered_map>
#include <SceneRuntime/ScenePhysics.h>
#include <SceneRuntime/ScriptRuntime.h>
#include <SceneRuntime/PhysicsWorld.h>
#include <Engine/Graphics/Renderers/OcclusionRenderer.h>

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
        bool SetParent(std::string_view id, const std::string& parentId, std::string& error);
        bool SetParents(const std::vector<std::string>& ids,const std::string& parentId,std::string& error);
        bool SetComponentBatch(const std::vector<ScenePlacement>& settings,const std::filesystem::path& assetsRoot,std::string& error);
        // 表示名だけを変更します。ID・描画リソースは維持します。
        // Component changes are transactional. Call outside Render after GPU idle.
        bool SetSettings(const SceneSettings& settings, std::string& error);
        bool SetComponents(std::string_view id, const ScenePlacement& settings,
            const std::filesystem::path& assetsRoot, std::string& error);
        // Runtime-only update; elapsed seconds rotates enabled Rotators in local coordinates.
        bool UpdateComponents(double seconds);
        bool QueueScriptEvent(ScriptEvent event,std::string& error);
        /// <summary>成功した更新で予約されたシーン操作を取り出します。</summary>
        std::vector<SceneCommand> TakeSceneCommands() { return scripts_.TakeSceneCommands(); }
        /// <summary>成功したフレームのScript UI変更を取得します。</summary>
        ScriptUiCommands TakeUiCommands() {return scripts_.TakeUiCommands();}
        /// <summary>失敗フレームの未適用Script UI変更を破棄します。</summary>
        void DiscardUiCommands() {scripts_.DiscardUiCommands();}
        void SetRuntimePreparation(std::function<bool(const SceneLayout&,std::string&)> prepare) { prepareRuntime_=std::move(prepare); }
        /// <summary>Scriptと姿勢の確定前にUI変更の描画資源を候補へ準備します。</summary>
        void SetUiPreparation(std::function<bool(const SceneLayout&,const ScriptUiCommands&,std::string&)> prepare) {prepareUi_=std::move(prepare);}
        /// <summary>現在のUI候補準備を取得し、検証処理を追加して委譲できます。</summary>
        const std::function<bool(const SceneLayout&,const ScriptUiCommands&,std::string&)>& UiPreparation() const {return prepareUi_;}
        std::string AnimatorStateName(const std::string& id) const { const auto found=animatorStates_.find(id); return found==animatorStates_.end() ? std::string{} : found->second.current; }
        /// <summary>現在のクリップ重み・同期位相・骨格姿勢を取得します。変更操作後に再取得してください。</summary>
        const AnimatorState* AnimatorStatus(const std::string& id) const { const auto found=animatorStates_.find(id); return found==animatorStates_.end() ? nullptr : &found->second; }
        /// <summary>個体のAnimatorパラメーターを実行中だけ上書きします。保存したComponentには反映しません。</summary>
        bool SetAnimatorParameter(const std::string& id,const std::string& name,float value);
        /// <summary>個体のパラメーター上書きを解除し、入力または保存値を使用します。</summary>
        bool ClearAnimatorParameter(const std::string& id,const std::string& name);
        void SetInputActions(std::map<std::string,float> values,std::map<std::string,bool> pressed) { inputValues_=std::move(values); inputPressed_=std::move(pressed); }
        /// <summary>フォーカス喪失や一時停止で未処理の固定更新入力を破棄します。</summary>
        void DiscardPendingInput() { fixedPressed_.clear(); fixedJump_=false; inputValues_.clear(); inputPressed_.clear(); scripts_.DiscardUiCommands(); }
        // Normalized input moves controllers on their parent-local XZ plane.
        bool MovePlayers(double seconds, float horizontal, float vertical, bool jump=false);
        bool AddImpulse(const std::string& id,const std::array<float,3>& impulse);
        /// <summary>選択モデルの現在の骨格姿勢からラグドール用剛体とJointを一括生成します。</summary>
        bool GenerateRagdoll(const std::string& id,const std::filesystem::path& root,std::string& error);
        std::optional<PhysicsRayHit> PhysicsRaycast(const std::array<float,3>& origin,const std::array<float,3>& direction,float distance,
            unsigned int mask=0xffffffffu,bool triggers=false,const std::string& ignore={}) const { return physicsWorld_.Raycast(origin,direction,distance,mask,triggers,ignore); }
        /// <summary>保存済みトラックを時計から評価し、3D配置へ一括反映します。</summary>
        bool Animate(const std::map<std::string, float>& clocks);
        size_t RenderVersion() const {return renderVersion_;}
        double PhysicsStepSeconds() const {return FixedStep();}
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
        /// <summary>描画補間した親子の回転だけを合成し、Scaleと反転を除外します。</summary>
        bool RenderWorldRotation(std::string_view id,DirectX::XMFLOAT4X4& matrix) const;
        bool WorldMatrix(std::string_view id, DirectX::XMFLOAT4X4& matrix) const;
        /// <summary>確定した物理配置を変えず、補間済みの描画用世界行列を取得します。</summary>
        bool RenderWorldMatrix(std::string_view id,DirectX::XMFLOAT4X4& matrix) const;
        // Convert a world matrix to the parent-relative local SRT, preserving output on failure.
        bool LocalTransformFromWorld(std::string_view id, const DirectX::XMFLOAT4X4& world, ScenePlacement& placement) const;
        bool WorldBounds(std::string_view id, std::array<std::array<float, 3>, 8>& corners) const;
    private:
        struct PreparedLayout
        {
            SceneLayout layout;
            std::shared_ptr<Engine::AssetDatabase> assets;
            std::map<std::filesystem::path,std::shared_ptr<const Engine::Material>> materials;
            std::filesystem::path assetsRoot;
            std::shared_ptr<Engine::ShadowMap> shadow,localShadow;
            std::vector<Engine::Object3D> objects;
            std::map<std::string,std::shared_ptr<Engine::ModelRenderer>> animated;
            std::map<std::string,AnimatorState> states;
            std::map<std::string,std::vector<std::shared_ptr<const Engine::ModelRenderer>>> lods;
        };
        struct LayoutView {
            const decltype(PreparedLayout::layout)& layout;
            const decltype(PreparedLayout::objects)& objects;
            const decltype(PreparedLayout::assetsRoot)& root;
            const decltype(PreparedLayout::assets)& assets;
            const decltype(PreparedLayout::materials)& materials;
            const decltype(PreparedLayout::animated)& animated;
            const decltype(PreparedLayout::states)& states;
            const decltype(PreparedLayout::lods)& lods;
        };
        LayoutView BaselineView(const PreparedLayout* previous) const;
        struct AnimatorFrame
        {
            std::string id;
            size_t index=0;
            std::shared_ptr<Engine::ModelRenderer> model;
            AnimatorState state;
            Engine::ModelRenderer::PreparedPose pose;
            bool applyPose=false;
        };
        Engine::Object3D BuildLayoutObject(const ScenePlacement& placement,const SceneLayout& layout,const DirectX::XMFLOAT4X4& matrix,PreparedLayout& prepared,const LayoutView& baseline,bool preserveExecution);
        void InitializeAnimatorObject(Engine::Object3D& object,const ScenePlacement& placement,const SceneLayout& layout,const DirectX::XMFLOAT4X4& matrix,PreparedLayout& prepared,const LayoutView& baseline,bool preserveExecution);
        static void ApplyAnimatorOverrides(const std::string& id,const ScriptRuntime& scripts,AnimatorState& state);
        std::map<std::string,float> AnimatorInputs(const SceneLayout& layout,const std::string& id) const;
        static void AdvanceAnimatorFrame(const SceneLayout& layout,const ScenePlacement& placement,AnimatorFrame& frame,double seconds,const std::map<std::string,float>& parameters,const DirectX::XMFLOAT4X4& matrix);
        /// <summary>配置・モデル・初期Animatorを候補へ構築し、現在のシーンを保持します。</summary>
        bool PrepareLayout(SceneLayout layout,const std::filesystem::path& assetsRoot,std::string& error,bool preserveExecution,PreparedLayout& result,const PreparedLayout* previous=nullptr);
        /// <summary>実行段階の変更を直前の候補から準備し、進行したAnimator状態を保持します。</summary>
        bool PrepareScriptLayout(SceneLayout layout,const PreparedLayout* previous,PreparedLayout& result,std::string& error);
        /// <summary>現在の実行リソースと状態を更新候補へ参照共有します。</summary>
        PreparedLayout RuntimeLayout() const;
        /// <summary>固定コールバック・物理・衝突イベントを候補へ進めます。</summary>
        bool FixedTick(SceneLayout& layout,ScenePhysics::States& states,ScriptRuntime& scripts,float horizontal,float vertical,bool jump,
            const std::map<std::string,bool>& pressed,std::string& error);
        /// <summary>接触イベントを指定した実行状態へ予約します。</summary>
        bool QueueContacts(const SceneLayout& layout,ScriptRuntime& scripts,std::string& error) const;
        /// <summary>準備した配置とリソースをシーンへ反映します。</summary>
        void CommitLayout(PreparedLayout&& prepared,bool preserveExecution);
        bool PrepareRootMotion(PreparedLayout& prepared,std::vector<AnimatorFrame>& frames,std::string& error) const;
        /// <summary>全個体の姿勢とイベントを検証し、Scriptの候補キューへ予約します。</summary>
        bool PrepareAnimators(const SceneLayout& layout,double seconds,const std::map<std::string,std::shared_ptr<Engine::ModelRenderer>>& models,
            const std::map<std::string,AnimatorState>& states,ScriptRuntime& scripts,std::vector<AnimatorFrame>& result,std::string& error) const;
        std::map<std::string,bool> FixedRootOverrides(SceneLayout& layout,const ScriptRuntime& scripts) const;
        bool SynchronizeRagdolls(SceneLayout& layout,std::string& error) const;
        static std::map<std::string,std::array<bool,3>> DisableRagdollOwners(SceneLayout& layout,ScenePhysics::States& states);
        bool AdvanceLegacyPlayers(SceneLayout& layout,ScenePhysics::States& states,float horizontal,float vertical,bool jump) const;
        static void RestoreRagdollOwners(SceneLayout& layout,const std::map<std::string,std::array<bool,3>>& ragdollOwnerEnabled);
        static void RotateComponents(SceneLayout& candidate,double seconds);
        bool UpdateLateComponents(double seconds,PreparedLayout& prepared,ScriptRuntime& runtime,std::vector<AnimatorFrame>& frames,std::map<std::string,std::array<float,3>>& impulses,std::string& error);
        static bool ApplyRootDelta(ScenePlacement& placement,const AnimatorFrame& frame);
        static void ConstrainRootFrames(PreparedLayout& prepared,std::vector<AnimatorFrame>& frames,const std::vector<DirectX::XMFLOAT4X4>& matrices);
        Engine::Object3D BuildAddedObject(const ScenePlacement& placement,const SceneLayout& validation,const DirectX::XMFLOAT4X4& matrix,const std::filesystem::path& assetsRoot,PreparedLayout& prepared);
        static void RemapDuplicateLinks(ScenePlacement& placement,const std::unordered_map<std::string,std::string>& copies);
        void InitializeAddedAnimator(Engine::Object3D& object,const ScenePlacement& placement,const SceneLayout& validation,const DirectX::XMFLOAT4X4& matrix,const std::shared_ptr<const Engine::ModelRenderer>& model,PreparedLayout& prepared);
        void CommitAnimatorFrames(std::vector<AnimatorFrame>& frames);
        static bool SameRenderResources(const SceneLayout& layout,const SceneLayout& before);
        static void ValidateAnimatorModels(const PreparedLayout& candidate);
        struct ResourceChanges {bool resolveAssets=false,animatedChange=false;};
        static ResourceChanges DetectResourceChanges(const SceneLayout& layout,const SceneLayout& before,const std::unordered_map<std::string,size_t>& old);
        static Engine::Object3D ReuseRuntimeObject(const ScenePlacement& p,PreparedLayout& candidate,const LayoutView& baseline,size_t index);
        Engine::Object3D BuildStaticRuntimeObject(const ScenePlacement& p,PreparedLayout& candidate);
        bool PrepareRuntimeObjects(PreparedLayout& candidate,const LayoutView& baseline,const std::unordered_map<std::string,size_t>& old);
        mutable MeshTelemetry meshTelemetry_;
        std::filesystem::path assetsRoot_;
        std::shared_ptr<Engine::AssetDatabase> assetIndex_;
        std::map<std::filesystem::path,std::shared_ptr<const Engine::Material>> materials_;
        std::shared_ptr<Engine::GpuRetirement> retirement_;
        std::function<bool(const SceneLayout&,std::string&)> prepareRuntime_;
        std::function<bool(const SceneLayout&,const ScriptUiCommands&,std::string&)> prepareUi_;
        bool InitializeModels(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& shaderPath,
            std::string* error);
        bool ReparentPlacement(ScenePlacement& placement, std::string parentId, std::string& error) const;
        bool TranslatePlacement(ScenePlacement& placement, const std::array<float,3>& delta) const;
        /// <summary>配置の世界行列を準備し、検証済みの前回配置がある場合は未変更の行列を再利用します。</summary>
        static bool PrepareTransforms(const SceneLayout& layout, std::vector<Engine::Object3D>& objects, std::string& error,const SceneLayout* validatedBefore=nullptr);
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
        struct PhysicalPose { std::array<float,3> position{},rotation{},scale{1,1,1}; std::string parent; };
        /// <summary>移動体の確定したローカル姿勢だけを保存します。</summary>
        static std::map<std::string,PhysicalPose> PhysicalPoses(const SceneLayout& layout);
        /// <summary>補間の行列を描画用に解決し、テレポートは即座に反映します。</summary>
        bool PrepareRenderTransforms() const;
        /// <summary>同じ補間条件で描画用のローカル配置と回転を準備します。</summary>
        bool InterpolatedLocalTransform(const ScenePlacement& placement,DirectX::XMFLOAT4X4& transform,DirectX::XMFLOAT4X4& rotation) const;
        std::map<std::string,PhysicalPose> previousPhysical_,currentPhysical_;
        size_t renderVersion_=0;
        mutable size_t preparedRenderVersion_=static_cast<size_t>(-1);
        mutable std::vector<DirectX::XMFLOAT4X4> renderMatrices_;
        double fixedSeconds_=0;
        bool fixedJump_=false;
        std::map<std::string,bool> fixedPressed_;
        ScenePhysics::States physics_;
        PhysicsWorld physicsWorld_;
        Microsoft::WRL::ComPtr<ID3D12Device> materialDevice_;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> materialQueue_;
        bool modelsReady_ = false;
        std::shared_ptr<Engine::ShadowMap> shadow_,localShadow_;
        std::filesystem::path meshShader_;
        bool PrepareShadows(const SceneSettings& settings,std::shared_ptr<Engine::ShadowMap>& directional,std::shared_ptr<Engine::ShadowMap>& local,std::string& error) const;
        double FixedStep() const {return layout_.settings.fixedStep==1.0f/60.0f ? 1.0/60.0 : static_cast<double>(layout_.settings.fixedStep);}
        mutable Engine::OcclusionRenderer occlusion_;
        SceneLayout layout_;
        Engine::ModelManager models_;
        std::vector<Engine::Object3D> objects_;
        std::map<std::string,std::vector<std::shared_ptr<const Engine::ModelRenderer>>> lodModels_;
    };
}
