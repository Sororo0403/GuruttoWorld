#pragma once
#include <Engine/Assets/AssetDatabase.h>
#include <Engine/Graphics/DirectX12/GpuRetirement.h>
#include <SceneRuntime/Scene/SceneLayout.h>
#include <SceneRuntime/Scene/SceneDocumentData.h>
#include <Engine/Graphics/Models/ModelManager.h>
#include <Engine/Graphics/Models/Object3D.h>
#include <optional>
#include <unordered_map>
#include <SceneRuntime/Physics/ScenePhysics.h>
#include <SceneRuntime/Scene/SceneExecutionState.h>
#include <SceneRuntime/Physics/PhysicsWorld.h>
#include <Engine/Graphics/Renderers/OcclusionRenderer.h>

namespace Engine
{
    class DirectX12Renderer;
}
namespace SceneRuntime
{
    struct UiState;
}
namespace SceneRuntime
{
    /// <summary>直前のシーン描画の呼び出し数と三角形数を保持します。</summary>
    struct MeshTelemetry
    {
        size_t draws = 0, triangles = 0;
    };
    /// <summary>シーンの配置と描画資源を管理します。実行用ワールドは物理とスクリプトの状態も所有します。</summary>
    class SceneWorld final
    {
    public:
        /// <summary>独立した実行データと実行状態を所有するワールドを作成します。</summary>
        SceneWorld()
            : execution_(std::make_unique<SceneExecutionState>()), document_(std::make_shared<SceneDocumentData>()),
              layout_(document_->layout_)
        {
        }
        /// <summary>外部ドキュメントの描画投影を作成します。実行状態は所有しません。</summary>
        explicit SceneWorld(std::shared_ptr<SceneDocumentData> document);
        /// <summary>初期化前に所有者の固定した参照情報を受け取ります。</summary>
        void SetContentContext(std::shared_ptr<const Engine::ContentContext> context);
        /// <summary>このワールドが使用する固定した参照情報を取得します。</summary>
        const std::shared_ptr<const Engine::ContentContext>& Content() const
        {
            return context_;
        }
        /// <summary>実行用ではスクリプトを停止し、編集用ではドキュメントから投影先の参照を外します。</summary>
        /// <remarks>描画資源が GPU から参照されていないことを、呼び出し側で確認してから破棄してください。</remarks>
        ~SceneWorld();
        /// <summary>修復可能な編集用の投影かを返します。</summary>
        bool IsDocumentView() const
        {
            return !execution_;
        }
        /// <summary>この実行セッションの定義集合で、現在のスクリプトを検証します。</summary>
        bool ValidateScripts(std::string& error) const
        {
            return execution_ &&
                   ScriptRuntime::ValidateForPlay(layout_, error, &execution_->values.scripts.Definitions());
        }
        /// <summary>この実行セッションに指定段階のコールバックがあるかを返します。</summary>
        bool HasScriptPhase(ScriptPhase phase) const
        {
            return execution_ && ScriptRuntime::HasPhase(layout_, phase, &execution_->values.scripts.Definitions());
        }
        /// <summary>最後の投影で取得できなかった資源の診断を返します。</summary>
        const std::vector<std::string>& ResourceDiagnostics() const
        {
            return resourceDiagnostics_;
        }
        /// <summary>描画を準備してシーンを読みます。編集用の場合だけ旧外部参照の修復を待ちます。</summary>
        bool Initialize(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& assetsRoot,
                        const std::filesystem::path& layoutPath, const std::filesystem::path& shaderPath,
                        std::string* error = nullptr, bool allowReferenceMigration = false);
        /// <summary>取得済みの配置から描画資源を初期化します。配置ファイルを読み直しません。</summary>
        bool Initialize(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& assetsRoot,
                        SceneLayout layout, const std::filesystem::path& shaderPath, std::string* error = nullptr);
        /// <summary>保存ファイルからシーンを読み直し、準備成功時だけ置き換えます。旧外部参照の移行を許可する場合は診断を保持します。</summary>
        /// <remarks>GPU の完了を待ち、Render の外で呼び出してください。</remarks>
        bool Reload(const std::filesystem::path& assetsRoot, const std::filesystem::path& layoutPath,
                    std::string& error, bool allowReferenceMigration = false);
        /// <summary>取得済みの配置を解決し、配置ファイルを再取得せずに配置と描画資源を交換します。</summary>
        bool ReloadLayout(const std::filesystem::path& assetsRoot, SceneLayout layout, std::string& error,
                          bool allowReferenceMigration = false);
        /// <summary>未保存の現在の配置から描画資源を再構築します。準備失敗時は現在の配置と資源を保持します。</summary>
        /// <remarks>GPU の完了を待ち、Render の外で呼び出してください。</remarks>
        bool ReloadAssets(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& assetsRoot,
                          const std::filesystem::path& shaderPath, std::string& error);
        /// <summary>取得済みの配置と描画資源を候補で準備し、成功時だけ反映します。preserveExecution で存続する実行状態を引き継ぎます。</summary>
        bool ReplaceLayout(SceneLayout layout, const std::filesystem::path& assetsRoot, std::string& error,
                           bool preserveExecution = false);
        /// <summary>遷移先のスクリプト開始と描画準備を候補で完了し、成功時だけ旧実行状態を置き換えます。</summary>
        bool ReplaceRuntimeLayout(SceneLayout layout, const std::filesystem::path& assetsRoot, std::string& error);
        /// <summary>固定・通常・LateUpdateをまとめ、失敗時は物理・入力蓄積・実行状態を復元します。</summary>
        /// <param name="advancePhysics">true の場合は固定更新と物理も進めます。false の場合は通常更新と LateUpdate だけを実行します。</param>
        bool UpdateFrame(double seconds, float horizontal, float vertical, bool jump, bool advancePhysics);
        /// <summary>現在の配置を補間した描画行列で描画し、描画呼び出し数と三角形数を記録します。</summary>
        void Draw(ID3D12GraphicsCommandList* commands, const Engine::Camera& camera,
                  const Engine::DirectionalLight& light, const UiState* state = nullptr) const;
        const MeshTelemetry& Telemetry() const
        {
            return meshTelemetry_;
        }
        const SceneLayout& Layout() const
        {
            return layout_;
        }
        /// <summary>スクリプトが所有する音声などの外部資源を一時停止・再開します。</summary>
        void PauseScripts(bool paused);
        /// <summary>親基準の位置・回転・スケールを配置と描画へ一括反映します。回転はラジアンで指定し、失敗時は変更しません。</summary>
        bool SetLocalTransform(std::string_view id, const std::array<float, 3>& position,
                               const std::array<float, 3>& rotation, const std::array<float, 3>& scale);
        /// <summary>ワールド行列を親基準の保存用 SRT に変換して反映します。表現できないせん断がある場合は変更しません。</summary>
        bool SetWorldTransform(std::string_view id, const DirectX::XMFLOAT4X4& matrix);
        /// <summary>ワールド空間の位置・回転・スケールから行列を作り、親基準の SRT へ変換して反映します。回転はラジアンです。</summary>
        bool SetWorldTransform(std::string_view id, const std::array<float, 3>& position,
                               const std::array<float, 3>& rotation, const std::array<float, 3>& scale);
        /// <summary>ワールド座標の移動量を全対象へ一括適用し、失敗時は変更しません。選択した子孫は選択した祖先に一度だけ追従します。</summary>
        bool TranslateObjectsWorld(const std::vector<std::string>& ids, const std::array<float, 3>& delta);
        /// <summary>ワールド座標の支点を中心に、選択した枝ごとに一度回転します。ローカルのせん断を表現できない場合は全対象を変更しません。</summary>
        bool RotateObjectsWorld(const std::vector<std::string>& ids, const std::array<float, 3>& pivot,
                                const DirectX::XMFLOAT4X4& rotation);
        /// <summary>正規直交するワールド軸に沿って、支点を中心に正の倍率で拡縮します。選択した子孫は祖先の拡縮に一度だけ追従します。</summary>
        bool ScaleObjectsWorld(const std::vector<std::string>& ids, const std::array<float, 3>& pivot,
                               const DirectX::XMFLOAT4X4& axes, const std::array<float, 3>& factors);
        /// <summary>ワールド姿勢を保って親を変更します。循環や表現できないローカル変換は変更せず拒否します。</summary>
        bool SetParent(std::string_view id, const std::string& parentId, std::string& error);
        /// <summary>全対象のワールド姿勢を保って親を一括変更します。変更後の階層を検証し、失敗時は全対象を保持します。</summary>
        bool SetParents(const std::vector<std::string>& ids, const std::string& parentId, std::string& error);
        /// <summary>各 ID のコンポーネント構成を候補で検証して一括更新します。個体の名前・階層・姿勢は維持します。</summary>
        /// <remarks>描画資源を交換する場合があるため、GPU の完了後、Render の外で呼び出してください。</remarks>
        bool SetComponentBatch(const std::vector<ScenePlacement>& settings, const std::filesystem::path& assetsRoot,
                               std::string& error);
        /// <summary>影の描画資源を準備してシーン設定を更新します。固定時間刻みの変更時は、蓄積時間と物理補間履歴をリセットします。</summary>
        bool SetSettings(const SceneSettings& settings, std::string& error);
        /// <summary>指定 ID のコンポーネント構成だけを更新します。複数対象の更新と同じ検証・資源準備を行います。</summary>
        /// <remarks>GPU の完了後、Render の外で呼び出してください。</remarks>
        bool SetComponents(std::string_view id, const ScenePlacement& settings, const std::filesystem::path& assetsRoot,
                           std::string& error);
        /// <summary>未公開の実行ワールドで開始処理と派生資源を準備し、成功時だけ反映します。</summary>
        bool StartScripts(std::string& error);
        /// <summary>実行用ワールドの回転・通常スクリプト・アニメーション・LateUpdate を候補で進め、成功時だけ反映します。</summary>
        /// <param name="seconds">有限の正の経過秒数。固定更新と物理の進行は MovePlayers または UpdateFrame が担当します。</param>
        bool UpdateComponents(double seconds);
        /// <summary>実行セッションの次のスクリプト更新へイベントを予約します。無効なイベントや実行状態のない投影では失敗します。</summary>
        bool QueueScriptEvent(ScriptEvent event, std::string& error);
        /// <summary>成功した更新で予約されたシーン操作を取り出します。</summary>
        std::vector<SceneCommand> TakeSceneCommands()
        {
            return execution_ && CanMutateScene() ? execution_->values.scripts.TakeSceneCommands()
                                                  : std::vector<SceneCommand>{};
        }
        /// <summary>成功したフレームのスクリプト UI変更を取得します。</summary>
        ScriptUiCommands TakeUiCommands()
        {
            return execution_ && CanMutateScene() ? execution_->values.scripts.TakeUiCommands() : ScriptUiCommands{};
        }
        /// <summary>フレーム更新が確定した後に実行する音声指示を取り出します。</summary>
        std::vector<AudioCommand> TakeAudioCommands()
        {
            return execution_ && CanMutateScene() ? execution_->values.scripts.TakeAudioCommands()
                                                  : std::vector<AudioCommand>{};
        }
        /// <summary>失敗または中断したフレームの音声指示を破棄します。</summary>
        void DiscardAudioCommands();
        /// <summary>失敗フレームの未適用スクリプト UI変更を破棄します。</summary>
        void DiscardUiCommands();
        /// <summary>実行候補の確定前に外側の資源を準備する処理を登録します。準備中のWorld変更は禁止し、失敗時は候補を反映しません。</summary>
        void SetRuntimePreparation(std::function<bool(const SceneLayout&, std::string&)> prepare);
        /// <summary>スクリプトと姿勢の確定前にUI変更の描画資源を候補へ準備します。</summary>
        void SetUiPreparation(std::function<bool(const SceneLayout&, const ScriptUiCommands&, std::string&)> prepare);
        /// <summary>現在のUI候補準備を取得し、検証処理を追加して委譲できます。</summary>
        const std::function<bool(const SceneLayout&, const ScriptUiCommands&, std::string&)>& UiPreparation() const
        {
            return prepareUi_;
        }
        std::string AnimatorStateName(const std::string& id) const
        {
            const auto found = animatorStates_.find(id);
            return found == animatorStates_.end() ? std::string{} : found->second.current;
        }
        /// <summary>現在のクリップ重み・同期位相・骨格姿勢を取得します。変更操作後に再取得してください。</summary>
        const AnimatorState* AnimatorStatus(const std::string& id) const
        {
            const auto found = animatorStates_.find(id);
            return found == animatorStates_.end() ? nullptr : &found->second;
        }
        /// <summary>個体のAnimatorパラメーターを実行中だけ上書きします。保存したコンポーネントには反映しません。</summary>
        bool SetAnimatorParameter(const std::string& id, const std::string& name, float value);
        /// <summary>個体のパラメーター上書きを解除し、入力または保存値を使用します。</summary>
        bool ClearAnimatorParameter(const std::string& id, const std::string& name);
        /// <summary>入力アクションの状態を設定します。</summary>
        void SetInputActions(std::map<std::string, float> values, std::map<std::string, bool> pressed);
        /// <summary>フォーカス喪失や一時停止で未処理の固定更新入力を破棄します。</summary>
        void DiscardPendingInput();
        /// <summary>蓄積した秒数を固定刻みで進め、スクリプト・物理・移動を反映します。移動入力は親のローカル XZ 平面を基準にします。</summary>
        bool MovePlayers(double seconds, float horizontal, float vertical, bool jump = false);
        /// <summary>動的剛体へ次回の物理更新で加えるワールド空間のインパルスを予約します。</summary>
        bool AddImpulse(const std::string& id, const std::array<float, 3>& impulse);
        /// <summary>選択モデルの現在の骨格姿勢からラグドール用剛体とJointを一括生成します。</summary>
        bool GenerateRagdoll(const std::string& id, const std::filesystem::path& root, std::string& error);
        /// <summary>実行中の物理ワールドで最も近い交点を取得します。実行状態がない場合は未設定を返します。</summary>
        std::optional<PhysicsRayHit> PhysicsRaycast(const std::array<float, 3>& origin,
                                                    const std::array<float, 3>& direction, float distance,
                                                    unsigned int mask = 0xffffffffu, bool triggers = false,
                                                    const std::string& ignore = {}) const
        {
            return execution_ ? execution_->physicsWorld.Raycast(origin, direction, distance, mask, triggers, ignore)
                              : std::nullopt;
        }
        /// <summary>保存済みトラックを時計から評価し、3D配置へ一括反映します。</summary>
        bool Animate(const std::map<std::string, float>& clocks);
        size_t RenderVersion() const
        {
            return renderVersion_;
        }
        double PhysicsStepSeconds() const
        {
            return FixedStep();
        }
        /// <summary>表示名だけを変更し、ID・描画資源は保持します。空白だけの名前や無効な ID は拒否します。</summary>
        bool RenameObject(std::string_view id, std::string name);
        /// <summary>配置と描画資源を準備してオブジェクトを追加します。ID が空の場合だけ新しい ID を発行します。</summary>
        /// <remarks>GPU の完了後、Render の外で呼び出してください。</remarks>
        bool AddObject(ScenePlacement placement, const std::filesystem::path& assetsRoot, std::string& createdId,
                       std::string& error);
        /// <summary>一つのオブジェクトを複製してワールド座標の移動量を加え、複製先の ID を createdId に設定します。</summary>
        bool DuplicateObject(std::string_view id, const std::array<float, 3>& offset, std::string& createdId,
                             std::string& error);
        /// <summary>選択対象を一度ずつ複製し、複製内の親とコンポーネント参照を付け替えます。各枝へワールド座標の移動量を一度だけ加えます。</summary>
        bool DuplicateObjects(const std::vector<std::string>& ids, const std::array<float, 3>& offset,
                              std::vector<std::string>& createdIds, std::string& error);
        /// <summary>指定した ID だけを一括削除します。残る子はワールド姿勢を保ってルートへ移し、削除した描画資源は GPU 完了まで保持します。</summary>
        bool RemoveObjects(const std::vector<std::string>& ids, std::string& error);
        /// <summary>ワールド空間のレイと描画形状の交差を調べ、指定距離内で最も近いオブジェクトの ID を返します。</summary>
        std::optional<std::string> PickRay(const std::array<float, 3>& origin, const std::array<float, 3>& direction,
                                           float maxDistance = 220.0f) const;
        /// <summary>祖先からの回転だけを合成し、スケール・反転・せん断を除いたワールド回転を取得します。</summary>
        bool WorldRotation(std::string_view id, DirectX::XMFLOAT4X4& matrix) const;
        /// <summary>描画補間した親子の回転だけを合成し、スケールと反転を除外します。</summary>
        bool RenderWorldRotation(std::string_view id, DirectX::XMFLOAT4X4& matrix) const;
        /// <summary>親子の変換を含む確定済みのワールド行列を取得します。描画補間は適用しません。</summary>
        bool WorldMatrix(std::string_view id, DirectX::XMFLOAT4X4& matrix) const;
        /// <summary>読み込み済みモデルのインポート設定を含む、骨格座標から世界座標への変換を取得します。</summary>
        bool SkeletonWorldMatrix(std::string_view id, DirectX::XMFLOAT4X4& matrix) const;
        /// <summary>確定した物理配置を変えず、補間済みの描画用世界行列を取得します。</summary>
        bool RenderWorldMatrix(std::string_view id, DirectX::XMFLOAT4X4& matrix) const;
        /// <summary>ワールド行列を親基準のローカル SRT に変換します。せん断などで変換できない場合は出力を保持します。</summary>
        bool LocalTransformFromWorld(std::string_view id, const DirectX::XMFLOAT4X4& world,
                                     ScenePlacement& placement) const;
        /// <summary>モデル境界箱の八頂点を確定済みのワールド行列で変換します。モデルがない個体には選択用の小さな箱を使います。</summary>
        bool WorldBounds(std::string_view id, std::array<std::array<float, 3>, 8>& corners) const;

    private:
        /// <summary>フレームの候補所有中に、外側から確定済みシーンを書き換える操作を拒否します。</summary>
        bool CanMutateScene(std::string* error = nullptr) const;
        struct PreparedLayout
        {
            SceneLayout layout;
            std::vector<std::string> diagnostics;
            std::shared_ptr<Engine::AssetDatabase> assets;
            std::map<std::filesystem::path, std::shared_ptr<const Engine::Material>> materials;
            std::filesystem::path assetsRoot;
            std::shared_ptr<Engine::ShadowMap> shadow, localShadow;
            std::vector<Engine::Object3D> objects;
            std::map<std::string, std::shared_ptr<Engine::ModelRenderer>> animated;
            std::map<std::string, AnimatorState> states;
            std::map<std::string, std::vector<std::shared_ptr<const Engine::ModelRenderer>>> lods;
        };
        struct LayoutView
        {
            const decltype(PreparedLayout::layout)& layout;
            const decltype(PreparedLayout::objects)& objects;
            const decltype(PreparedLayout::assetsRoot)& root;
            const decltype(PreparedLayout::assets)& assets;
            const decltype(PreparedLayout::materials)& materials;
            const decltype(PreparedLayout::animated)& animated;
            const decltype(PreparedLayout::states)& states;
            const decltype(PreparedLayout::lods)& lods;
        };
        /// <summary>候補の参照解決を行い、編集時は欠損理由を保持します。</summary>
        bool PrepareLayoutAssets(SceneLayout& layout, PreparedLayout& prepared, const PreparedLayout* previous,
                                 bool configurationCurrent, std::map<std::string, std::string>& unresolved);
        /// <summary>配置ごとの描画資源を作り、編集時の欠損を代替表示へ変換します。</summary>
        void PrepareLayoutObjects(const SceneLayout& layout, const std::vector<DirectX::XMFLOAT4X4>& matrices,
                                  PreparedLayout& prepared, const LayoutView& baseline, bool preserveExecution,
                                  bool indexAvailable, const std::map<std::string, std::string>& unresolved);
        /// <summary>編集用の配置に欠損した参照がないか確認します。</summary>
        void ValidateEditingResources(const ScenePlacement& placement, bool indexAvailable,
                                      const std::map<std::string, std::string>& unresolved) const;
        /// <summary>資源に変更がない候補を共有し、配置とAnimatorの整合性を確認します。</summary>
        static bool PrepareCachedLayout(SceneLayout layout, const LayoutView& baseline, PreparedLayout& result,
                                        std::string& error);
        /// <summary>描画とUIの候補を順に準備します。</summary>
        bool PrepareRuntimeCallbacks(const SceneLayout& layout, const ScriptRuntime& runtime, std::string& error) const;
        /// <summary>複製先専用のAnimator資源と姿勢を作ります。</summary>
        void DuplicateAnimator(size_t index, const std::string& id, Engine::Object3D& object,
                               std::map<std::string, std::shared_ptr<Engine::ModelRenderer>>& animated,
                               std::map<std::string, AnimatorState>& states);
        LayoutView BaselineView(const PreparedLayout* previous) const;
        struct AnimatorFrame
        {
            std::string id;
            size_t index = 0;
            std::shared_ptr<Engine::ModelRenderer> model;
            AnimatorState state;
            Engine::ModelRenderer::PreparedPose pose;
            bool applyPose = false;
        };
        /// <summary>固定更新と可変更新を一つの候補・物理取引で準備し、一度だけ確定します。</summary>
        bool ExecuteUpdate(double seconds, float horizontal, float vertical, bool jump, bool movement, bool components);
        /// <summary>共有する未確定候補へ固定更新と入力の繰越を適用します。</summary>
        bool PrepareMovement(SceneLayout& candidate, double seconds, float horizontal, float vertical, bool jump);
        /// <summary>未確定候補へ可変更新を適用し、描画投影と通知を準備します。</summary>
        bool PrepareComponents(SceneLayout candidate, double seconds, PreparedLayout& prepared,
                               std::vector<AnimatorFrame>& frames,
                               std::map<std::string, std::array<float, 3>>& impulses, std::string& error);
        /// <summary>セッションに固定したスクリプト定義で候補を検証します。</summary>
        bool ValidateScriptsCandidate(const SceneLayout& candidate, std::string& error) const;
        Engine::Object3D BuildLayoutObject(const ScenePlacement& placement, const SceneLayout& layout,
                                           const DirectX::XMFLOAT4X4& matrix, PreparedLayout& prepared,
                                           const LayoutView& baseline, bool preserveExecution);
        void InitializeAnimatorObject(Engine::Object3D& object, const ScenePlacement& placement,
                                      const SceneLayout& layout, const DirectX::XMFLOAT4X4& matrix,
                                      PreparedLayout& prepared, const LayoutView& baseline, bool preserveExecution);
        static void ApplyAnimatorOverrides(const std::string& id, const ScriptRuntime& scripts, AnimatorState& state);
        std::map<std::string, float> AnimatorInputs(const SceneLayout& layout, const std::string& id) const;
        static void AdvanceAnimatorFrame(const SceneLayout& layout, const ScenePlacement& placement,
                                         AnimatorFrame& frame, double seconds,
                                         const std::map<std::string, float>& parameters,
                                         const DirectX::XMFLOAT4X4& matrix);
        /// <summary>配置・モデル・初期Animatorを候補へ構築し、現在のシーンを保持します。</summary>
        bool PrepareLayout(SceneLayout layout, const std::filesystem::path& assetsRoot, std::string& error,
                           bool preserveExecution, PreparedLayout& result, const PreparedLayout* previous = nullptr);
        /// <summary>実行段階の変更を直前の候補から準備し、進行したAnimator状態を保持します。</summary>
        bool PrepareScriptLayout(SceneLayout layout, const PreparedLayout* previous, PreparedLayout& result,
                                 std::string& error);
        /// <summary>現在の実行リソースと状態を更新候補へ参照共有します。</summary>
        PreparedLayout RuntimeLayout() const;
        /// <summary>固定コールバック・物理・衝突イベントを候補へ進めます。</summary>
        bool FixedTick(SceneLayout& layout, ScenePhysics::States& states, ScriptRuntime& scripts, float horizontal,
                       float vertical, bool jump, const std::map<std::string, bool>& pressed, std::string& error);
        /// <summary>接触イベントを指定した実行状態へ予約します。</summary>
        bool QueueContacts(const SceneLayout& layout, ScriptRuntime& scripts, std::string& error) const;
        /// <summary>準備した配置とリソースをシーンへ反映します。</summary>
        void CommitLayout(PreparedLayout&& prepared, bool preserveExecution);
        bool PrepareRootMotion(PreparedLayout& prepared, std::vector<AnimatorFrame>& frames, std::string& error) const;
        /// <summary>全個体の姿勢とイベントを検証し、スクリプトの候補キューへ予約します。</summary>
        bool PrepareAnimators(const SceneLayout& layout, double seconds,
                              const std::map<std::string, std::shared_ptr<Engine::ModelRenderer>>& models,
                              const std::map<std::string, AnimatorState>& states, ScriptRuntime& scripts,
                              std::vector<AnimatorFrame>& result, std::string& error) const;
        std::map<std::string, bool> FixedRootOverrides(SceneLayout& layout, const ScriptRuntime& scripts) const;
        bool SynchronizeRagdolls(SceneLayout& layout, std::string& error) const;
        static std::map<std::string, std::array<bool, 3>> DisableRagdollOwners(SceneLayout& layout,
                                                                               ScenePhysics::States& states);
        bool AdvanceLegacyPlayers(SceneLayout& layout, ScenePhysics::States& states, float horizontal, float vertical,
                                  bool jump) const;
        static void RestoreRagdollOwners(SceneLayout& layout,
                                         const std::map<std::string, std::array<bool, 3>>& ragdollOwnerEnabled);
        static void RotateComponents(SceneLayout& candidate, double seconds);
        bool UpdateLateComponents(double seconds, PreparedLayout& prepared, ScriptRuntime& runtime,
                                  std::vector<AnimatorFrame>& frames,
                                  std::map<std::string, std::array<float, 3>>& impulses, std::string& error);
        static bool ApplyRootDelta(ScenePlacement& placement, const AnimatorFrame& frame);
        static void ConstrainRootFrames(PreparedLayout& prepared, std::vector<AnimatorFrame>& frames,
                                        const std::vector<DirectX::XMFLOAT4X4>& matrices);
        Engine::Object3D BuildAddedObject(const ScenePlacement& placement, const SceneLayout& validation,
                                          const DirectX::XMFLOAT4X4& matrix, const std::filesystem::path& assetsRoot,
                                          PreparedLayout& prepared);
        static void RemapDuplicateLinks(ScenePlacement& placement,
                                        const std::unordered_map<std::string, std::string>& copies);
        void InitializeAddedAnimator(Engine::Object3D& object, const ScenePlacement& placement,
                                     const SceneLayout& validation, const DirectX::XMFLOAT4X4& matrix,
                                     const std::shared_ptr<const Engine::ModelRenderer>& model,
                                     PreparedLayout& prepared);
        void CommitAnimatorFrames(std::vector<AnimatorFrame>& frames);
        static bool SameRenderResources(const SceneLayout& layout, const SceneLayout& before);
        static void ValidateAnimatorModels(const PreparedLayout& candidate);
        struct ResourceChanges
        {
            bool resolveAssets = false, animatedChange = false;
        };
        static ResourceChanges DetectResourceChanges(const SceneLayout& layout, const SceneLayout& before,
                                                     const std::unordered_map<std::string, size_t>& old);
        static Engine::Object3D ReuseRuntimeObject(const ScenePlacement& p, PreparedLayout& candidate,
                                                   const LayoutView& baseline, size_t index);
        Engine::Object3D BuildStaticRuntimeObject(const ScenePlacement& p, PreparedLayout& candidate);
        bool PrepareRuntimeObjects(PreparedLayout& candidate, const LayoutView& baseline,
                                   const std::unordered_map<std::string, size_t>& old);
        mutable MeshTelemetry meshTelemetry_;
        std::filesystem::path assetsRoot_;
        std::shared_ptr<Engine::AssetDatabase> assetIndex_;
        std::map<std::filesystem::path, std::shared_ptr<const Engine::Material>> materials_;
        std::shared_ptr<Engine::GpuRetirement> retirement_;
        std::function<bool(const SceneLayout&, std::string&)> prepareRuntime_;
        std::function<bool(const SceneLayout&, const ScriptUiCommands&, std::string&)> prepareUi_;
        bool InitializeModels(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& shaderPath,
                              std::string* error);
        bool ReparentPlacement(ScenePlacement& placement, std::string parentId, std::string& error) const;
        bool TranslatePlacement(ScenePlacement& placement, const std::array<float, 3>& delta) const;
        /// <summary>配置の世界行列を準備し、検証済みの前回配置がある場合は未変更の行列を再利用します。</summary>
        static bool PrepareTransforms(const SceneLayout& layout, std::vector<Engine::Object3D>& objects,
                                      std::string& error, const SceneLayout* validatedBefore = nullptr);
        bool SetPlacementTransform(std::string_view id, const ScenePlacement& placement);
        bool CommitTransforms(const SceneLayout& candidate);
        bool TransformObjectsWorld(const std::vector<std::string>& ids, const DirectX::XMFLOAT4X4& delta);
        std::string NewId(size_t& nextCounter) const;
        size_t nextObjectId_ = 1;
        void Append(ScenePlacement placement, Engine::Object3D object);
        /// <summary>削除した個体の物理状態と補間履歴を、実行所有者からまとめて除去します。</summary>
        void ForgetExecutionObject(const std::string& id);
        std::map<std::string, std::shared_ptr<Engine::ModelRenderer>> animatedModels_;
        std::map<std::string, AnimatorState> animatorStates_;
        std::unique_ptr<SceneExecutionState> execution_;
        struct FrameTransaction;
        using PhysicalPose = SceneExecutionState::PhysicalPose;
        /// <summary>移動体の確定したローカル姿勢だけを保存します。</summary>
        static std::map<std::string, PhysicalPose> PhysicalPoses(const SceneLayout& layout);
        /// <summary>補間の行列を描画用に解決し、テレポートは即座に反映します。</summary>
        bool PrepareRenderTransforms() const;
        /// <summary>同じ補間条件で描画用のローカル配置と回転を準備します。</summary>
        bool InterpolatedLocalTransform(const ScenePlacement& placement, DirectX::XMFLOAT4X4& transform,
                                        DirectX::XMFLOAT4X4& rotation) const;
        size_t renderVersion_ = 0;
        mutable size_t preparedRenderVersion_ = static_cast<size_t>(-1);
        mutable std::vector<DirectX::XMFLOAT4X4> renderMatrices_;
        Microsoft::WRL::ComPtr<ID3D12Device> materialDevice_;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> materialQueue_;
        bool modelsReady_ = false;
        std::shared_ptr<Engine::ShadowMap> shadow_, localShadow_;
        std::filesystem::path meshShader_;
        bool PrepareShadows(const SceneSettings& settings, std::shared_ptr<Engine::ShadowMap>& directional,
                            std::shared_ptr<Engine::ShadowMap>& local, std::string& error) const;
        double FixedStep() const
        {
            return layout_.settings.fixedStep == 1.0f / 60.0f ? 1.0 / 60.0
                                                              : static_cast<double>(layout_.settings.fixedStep);
        }
        mutable Engine::OcclusionRenderer occlusion_;
        std::shared_ptr<SceneDocumentData> document_;
        std::shared_ptr<const Engine::ContentContext> context_;
        SceneLayout& layout_;
        std::unique_ptr<SceneLayout> animationScratch_;
        std::vector<std::string> resourceDiagnostics_;
        Engine::ModelManager models_;
        std::vector<Engine::Object3D> objects_;
        std::map<std::string, std::vector<std::shared_ptr<const Engine::ModelRenderer>>> lodModels_;
    };
}
