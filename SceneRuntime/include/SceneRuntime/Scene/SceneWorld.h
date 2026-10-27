#pragma once
#include <Engine/Assets/AssetDatabase.h>
#include <SceneRuntime/Rendering/SceneResourcePreparation.h>
#include <SceneRuntime/Scene/SceneLayout.h>
#include <SceneRuntime/Scene/SceneDocumentData.h>
#include <SceneRuntime/Scene/WorldReadView.h>
#include <SceneRuntime/Scene/WorldState.h>
#include <Engine/Graphics/Models/ModelManager.h>
#include <Engine/Graphics/Models/Object3D.h>
#include <optional>
#include <SceneRuntime/Rendering/SceneViewQueries.h>
#include <SceneRuntime/Rendering/RenderTelemetry.h>
#include <unordered_map>
#include <SceneRuntime/Physics/PhysicsTypes.h>
#include <SceneRuntime/Scripts/ScriptSignals.h>
#include <SceneRuntime/Audio/AudioCommand.h>
#include <Engine/Graphics/Renderers/OcclusionRenderer.h>

namespace Engine
{
    class DirectX12Renderer;
}
namespace SceneRuntime
{
    struct UiState;
    struct RuntimeScriptEffects;
    struct RenderPacket;
    class SceneRenderState;
    class EditorSceneProjection;
}
namespace SceneRuntime
{
    /// <summary>編集・再生の確定境界を調停し、表示投影とGPU資源準備は専用の所有者へ委譲します。</summary>
    class SceneWorld final : public SceneEditTarget
    {
    public:
        /// <summary>独立した実行データと実行状態を所有するワールドを作成します。</summary>
        SceneWorld();
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
            return !runtimeOwner_;
        }
        /// <summary>この実行セッションの定義集合で、現在のスクリプトを検証します。</summary>
        bool ValidateScripts(std::string& error) const;
        /// <summary>この実行セッションに指定段階のコールバックがあるかを返します。</summary>
        bool HasScriptPhase(ScriptPhase phase) const;
        /// <summary>最後の投影で取得できなかった資源の診断を返します。</summary>
        const std::vector<std::string>& ResourceDiagnostics() const;
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
        /// <summary>描画へ公開した不変packetを保持します。World破棄後も表示値と資源の世代は有効です。</summary>
        std::shared_ptr<const RenderPacket> RenderSnapshot() const noexcept;
        /// <summary>表示所有者が記録した最後の描画統計を取得します。</summary>
        const MeshTelemetry& Telemetry() const;
        const SceneLayout& Layout() const override
        {
            return layout_;
        }
        /// <summary>描画に依存しない編集要求を準備し、投影の成功後に確定します。</summary>
        SceneEditResult Edit(const SceneEdit& edit, std::string& error) override;
        /// <summary>次の変更まで有効な読み取り専用ビューを取得します。</summary>
        WorldReadView Read() const noexcept;
        /// <summary>ローカル姿勢を検証し、成功した変更だけを返します。同じ値なら資源を準備しません。</summary>
        SceneEditResult ApplyLocalTransform(std::string_view id, const LocalTransform& transform);
        /// <summary>コンポーネントだけを置き換え、準備が成功した場合だけ変更結果を返します。</summary>
        SceneEditResult ApplyComponents(std::string_view id, const ScenePlacement& components,
                                        const std::filesystem::path& assetsRoot, std::string& error);
        /// <summary>Pauseの変更候補を準備し、失敗時は配置・実行値・通知を保持します。</summary>
        bool SetScriptsPaused(bool paused, std::string& error);
        /// <summary>確定済みのTransform変更を一度だけ取得します。フレーム準備中は既存通知も保持します。</summary>
        SceneChangeSet TakeChanges() noexcept;
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
        std::vector<SceneCommand> TakeSceneCommands();
        /// <summary>成功したフレームのスクリプトUI変更を取得します。</summary>
        ScriptUiCommands TakeUiCommands();
        /// <summary>確定したフレームで予約された音声指示を取得します。</summary>
        std::vector<AudioCommand> TakeAudioCommands();
        /// <summary>失敗または中断したフレームの音声指示を破棄します。</summary>
        void DiscardAudioCommands();
        /// <summary>失敗フレームの未適用スクリプト UI変更を破棄します。</summary>
        void DiscardUiCommands();
        /// <summary>実行候補の確定前に外側の資源を準備する処理を登録します。準備中のWorld変更は禁止し、失敗時は候補を反映しません。</summary>
        void SetRuntimePreparation(std::function<bool(const SceneLayout&, std::string&)> prepare);
        /// <summary>スクリプトと姿勢の確定前にUI変更の描画資源を候補へ準備します。</summary>
        void SetUiPreparation(std::function<bool(const SceneLayout&, const ScriptUiCommands&, std::string&)> prepare);
        /// <summary>候補の型付き実行値と変更通知から、表示・UIの公開を準備します。借用ビューを保持しないでください。</summary>
        void SetFramePreparation(
            std::function<bool(WorldState::ReadView, const SceneChangeSet&, const ScriptUiCommands&, std::string&)>
                prepare);
        /// <summary>型付きの表示準備を取得し、同じ候補の検証へ処理を追加できます。</summary>
        const std::function<bool(WorldState::ReadView, const SceneChangeSet&, const ScriptUiCommands&, std::string&)>&
        FramePreparation() const
        {
            return prepareFrame_;
        }
        /// <summary>現在のUI候補準備を取得し、検証処理を追加して委譲できます。</summary>
        const std::function<bool(const SceneLayout&, const ScriptUiCommands&, std::string&)>& UiPreparation() const
        {
            return prepareUi_;
        }
        /// <summary>確定済みのAnimator状態名を取得します。</summary>
        std::string AnimatorStateName(const std::string& id) const;
        /// <summary>現在のクリップ重み・同期位相・骨格姿勢を取得します。変更操作後に再取得してください。</summary>
        const AnimatorState* AnimatorStatus(const std::string& id) const;
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
                                                    const std::string& ignore = {}) const;
        /// <summary>保存済みトラックを時計から評価し、3D配置へ一括反映します。</summary>
        bool Animate(const std::map<std::string, float>& clocks);
        /// <summary>表示所有者が公開した世代番号を取得します。</summary>
        size_t RenderVersion() const;
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
        /// <summary>次の更新まで有効な表示問い合わせを、Worldに依存しない読取APIとして取得します。</summary>
        SceneViewQueries ViewQueries() const noexcept;
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
        /// <summary>固定した素材ルートを検証し、編集を文書セッションへ渡します。</summary>
        SceneEditResult ForwardDocumentEdit(const SceneEdit& edit, const std::filesystem::path& root,
                                            std::string& error);
        /// <summary>編集用に登録した文書セッションだけを取得します。</summary>
        SceneEditTarget* DocumentEditor() const noexcept;
        friend class ScenePresentation;
        class ChangePreparation final
        {
        public:
            /// <summary>準備中の再入と通知取得を禁止し、入れ子の状態は終了時に戻します。</summary>
            explicit ChangePreparation(bool& flag) noexcept : flag_(flag), previous_(flag)
            {
                flag_ = true;
            }
            /// <summary>準備前の状態を復元します。</summary>
            ~ChangePreparation()
            {
                Release();
            }
            /// <summary>準備所有権を一度だけ解放し、後続の取引を妨げません。</summary>
            void Release() noexcept
            {
                if (active_)
                {
                    flag_ = previous_;
                    active_ = false;
                }
            }

        private:
            bool& flag_;
            bool previous_;
            bool active_ = true;
        };
        using PreparedLayout = PreparedSceneResources;
        using LayoutView = SceneResourceView;
        using AnimatorFrame = SceneAnimatorFrame;
        struct RuntimeData;
        class RuntimeProjection;
        std::unique_ptr<RuntimeData> runtime_;
        std::unique_ptr<SceneRenderState> renderState_;
        /// <summary>新しいCPUセッションで配置と表示を準備し、成功時だけ所有者を交換します。</summary>
        bool ReplaceSession(SceneLayout layout, const std::filesystem::path& root, bool start, std::string& error);
        /// <summary>実行値と表示の候補を同じ境界で編集します。</summary>
        bool EditSession(const SceneEdit& edit, std::string& error, bool start = false);
        /// <summary>相対表記を正規化し、実行所有者と同じ素材ルートかを検証します。</summary>
        bool CheckSessionRoot(const std::filesystem::path& root, std::string& error) const;
        /// <summary>外部からの実行指示を、表示準備も含めて一括確定します。</summary>
        bool ApplySessionEffects(const RuntimeScriptEffects& effects);
        /// <summary>固定段階と可変段階を、共通セッションへ渡します。</summary>
        bool UpdateSession(double seconds, float horizontal, float vertical, bool jump, bool movement, bool components);
        /// <summary>確定済みの指示を表示所有者へ移し、CPU側の通知を取得します。</summary>
        void DrainSession() noexcept;
        mutable bool preparingChanges_ = false;
        /// <summary>フレームの候補所有中に、外側から確定済みシーンを書き換える操作を拒否します。</summary>
        bool CanMutateScene(std::string* error = nullptr) const;
        /// <summary>保存データの編集結果から、実行処理を起動せず表示packetだけを準備します。</summary>
        std::shared_ptr<const RenderPacket> PrepareDocumentPacket(const PreparedLayout& prepared, std::string& error);
        /// <summary>保存値を変えず、交換候補のGPU資源と現在の実行姿勢を表示packetへまとめます。</summary>
        std::shared_ptr<const RenderPacket> PrepareRuntimePacket(const PreparedLayout& prepared, std::string& error);
        /// <summary>編集投影の姿勢更新に用いる表示資源を共有し、表示packetを準備します。</summary>
        std::shared_ptr<const RenderPacket> PrepareDocumentPacket(const SceneLayout& layout,
                                                                  const std::vector<Engine::Object3D>& objects,
                                                                  std::string& error);
        /// <summary>資源に変更がない候補を共有し、配置とAnimatorの整合性を確認します。</summary>
        static bool PrepareCachedLayout(SceneLayout layout, const LayoutView& baseline, PreparedLayout& result,
                                        std::string& error);
        /// <summary>複製先専用のAnimator資源と姿勢を作ります。</summary>
        void DuplicateAnimator(size_t index, const std::string& id, Engine::Object3D& object,
                               std::map<std::string, std::shared_ptr<Engine::ModelRenderer>>& animated,
                               std::map<std::string, AnimatorState>& states);
        LayoutView BaselineView(const PreparedLayout* previous) const;
        /// <summary>固定更新と可変更新を一つの候補・物理取引で準備し、一度だけ確定します。</summary>
        bool ExecuteUpdate(double seconds, float horizontal, float vertical, bool jump, bool movement, bool components);
        /// <summary>配置・モデル・初期Animatorを候補へ構築し、現在のシーンを保持します。</summary>
        bool PrepareLayout(SceneLayout layout, const std::filesystem::path& assetsRoot, std::string& error,
                           bool preserveExecution, PreparedLayout& result, const PreparedLayout* previous = nullptr);
        /// <summary>実行段階の変更を直前の候補から準備し、進行したAnimator状態を保持します。</summary>
        bool PrepareScriptLayout(SceneLayout layout, const PreparedLayout* previous, PreparedLayout& result,
                                 std::string& error);
        /// <summary>現在の実行リソースと状態を更新候補へ参照共有します。</summary>
        PreparedLayout RuntimeLayout() const;
        /// <summary>準備した配置とリソースをシーンへ反映します。</summary>
        void CommitLayout(PreparedLayout&& prepared, bool preserveExecution);
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
        bool PrepareRuntimeObjects(PreparedLayout& candidate, const LayoutView& baseline,
                                   const std::unordered_map<std::string, size_t>& old);
        std::function<bool(const SceneLayout&, std::string&)> prepareRuntime_;
        std::function<bool(const SceneLayout&, const ScriptUiCommands&, std::string&)> prepareUi_;
        bool InitializeModels(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& shaderPath,
                              std::string* error);
        /// <summary>配置の世界行列を準備し、検証済みの前回配置がある場合は未変更の行列を再利用します。</summary>
        static bool PrepareTransforms(const SceneLayout& layout, std::vector<Engine::Object3D>& objects,
                                      std::string& error, const SceneLayout* validatedBefore = nullptr);
        bool SetPlacementTransform(std::string_view id, const ScenePlacement& placement);
        bool CommitTransforms(const SceneLayout& candidate);
        bool TransformObjectsWorld(const std::vector<std::string>& ids, const DirectX::XMFLOAT4X4& delta);
        size_t ownedNextId_ = 1;
        void Append(ScenePlacement placement, Engine::Object3D object);
        bool runtimeOwner_ = true;
        using PhysicalPose = SceneViewPose;
        SceneChangeSet ownedChanges_;
        std::function<bool(WorldState::ReadView, const SceneChangeSet&, const ScriptUiCommands&, std::string&)>
            prepareFrame_;
        double FixedStep() const
        {
            return layout_.settings.fixedStep == 1.0f / 60.0f ? 1.0 / 60.0
                                                              : static_cast<double>(layout_.settings.fixedStep);
        }
        std::shared_ptr<SceneDocumentData> document_;
        std::shared_ptr<const Engine::ContentContext> context_;
        SceneLayout ownedLayout_;
        std::unique_ptr<EditorSceneProjection> editorProjection_;
        size_t& nextObjectId_;
        SceneLayout& layout_;
        SceneChangeSet& changes_;
    };
}
