#pragma once
#include <SceneRuntime/Scripts/ScriptApi.h>

namespace SceneRuntime
{
    using ScriptDefinitions = std::map<std::string, ScriptDefinition>;

    class ScriptRegistry final
    {
    public:
        /// <summary>再生前にメインスレッドで登録します。名前の重複や不正なメタデータは拒否します。</summary>
        static bool Register(std::string name, ScriptDefinition definition);
        /// <summary>再生前にモジュール所有の処理を一括置換します。失敗時は既存の登録を保持します。</summary>
        static bool InstallModule(const std::string& owner, std::map<std::string, ScriptDefinition> definitions,
                                  std::string& error);
        /// <summary>現在の登録定義を参照します。登録やモジュール交換後は参照先の要素を再取得してください。</summary>
        static const std::map<std::string, ScriptDefinition>& Definitions();
        /// <summary>登録とモジュール交換の成功時に進む定義世代を取得します。</summary>
        static std::uint64_t Revision();
        /// <summary>実行セッションが独立して保持する、不変の定義集合を取得します。</summary>
        static std::shared_ptr<const ScriptDefinitions> Capture();
    };
    class ScriptRuntime final
    {
    public:
        /// <summary>指定した不変の定義集合を使います。未指定時は所有者の設定時に固定します。</summary>
        explicit ScriptRuntime(std::shared_ptr<const ScriptDefinitions> definitions = {})
            : definitions_(std::move(definitions))
        {
        }
        /// <summary>この実行セッションが使用する定義集合を取得します。</summary>
        const ScriptDefinitions& Definitions() const
        {
            return definitions_ ? *definitions_ : ScriptRegistry::Definitions();
        }
        /// <summary>全ライフサイクルのコールバックへ所有者の参照情報を渡します。</summary>
        void SetContentContext(std::shared_ptr<const Engine::ContentContext> content);
        /// <summary>有効なスクリプトの登録と保存値を、開始処理を呼ばずに検証します。</summary>
        static bool ValidateForPlay(const SceneLayout& layout, std::string& error,
                                    const ScriptDefinitions* definitions = nullptr);
        /// <summary>外側の候補上で開始処理だけを実行し、通常更新と時計を進めません。</summary>
        bool StartCandidate(SceneLayout& layout, std::string& error);
        /// <summary>指定した更新段階を候補で実行し、成功時だけ配置と実行状態を反映します。</summary>
        /// <remarks>構造変更は context.scene で予約し、コールバック中にオブジェクト配列の要素数を直接変更しないでください。</remarks>
        bool Update(SceneLayout& layout, double seconds, std::string& error,
                    const std::map<std::string, float>& input = {}, const std::map<std::string, bool>& pressed = {},
                    const PhysicsWorld* physics = nullptr, ScriptPhase phase = ScriptPhase::Update);
        /// <summary>外側で配置・実行状態・物理の取引候補を所有する更新を実行します。検証済みの変更前配置は差分検証に使用し、失敗時は全候補を破棄してください。</summary>
        bool UpdateCandidate(SceneLayout& layout, double seconds, std::string& error,
                             const std::map<std::string, float>& input = {},
                             const std::map<std::string, bool>& pressed = {}, const PhysicsWorld* physics = nullptr,
                             ScriptPhase phase = ScriptPhase::Update, const SceneLayout* validatedBefore = nullptr);
        /// <summary>指定した更新段階を持つ有効な処理があるか調べます。</summary>
        static bool HasPhase(const SceneLayout& layout, ScriptPhase phase,
                             const ScriptDefinitions* definitions = nullptr);
        /// <summary>外側の更新成功後に開始処理を確定し、予約した終了処理を一度だけ呼びます。</summary>
        void CommitStarts() noexcept;
        /// <summary>実行中のスクリプトへ一時停止と再開を通知します。</summary>
        void Pause(SceneLayout& layout, bool paused) noexcept;
        /// <summary>開始済みの処理を終了し、実行状態と予約を破棄します。終了コールバックの例外はログへ記録します。</summary>
        void Stop(SceneLayout& layout) noexcept;
        /// <summary>取り除かれた個体の終了処理を旧配置で実行し、残る個体の実行状態を維持します。</summary>
        void StopRemoved(SceneLayout& previous, const SceneLayout& next) noexcept;
        /// <summary>遷移で消える処理を候補から外し、終了処理は外側の確定まで予約します。</summary>
        void PrepareTransition(const SceneLayout& previous, SceneLayout& next);
        /// <summary>成功したフレームで予約されたシーン操作を取得します。</summary>
        std::vector<SceneCommand> TakeSceneCommands();
        /// <summary>成功したFixed・Update・LateUpdateが予約したUI変更を一度だけ取得します。</summary>
        ScriptUiCommands TakeUiCommands();
        /// <summary>成功した更新の音声指示を一度だけ取り出します。</summary>
        std::vector<AudioCommand> TakeAudioCommands();
        /// <summary>失敗または中断した更新の音声指示を破棄します。</summary>
        void DiscardAudioCommands()
        {
            audioCommands_.clear();
        }
        /// <summary>失敗フレームの未適用UI変更を破棄します。</summary>
        void DiscardUiCommands()
        {
            uiCommands_ = {};
        }
        /// <summary>未確定フレームのUI変更をGPU候補準備へ参照します。</summary>
        const ScriptUiCommands& UiCommands() const
        {
            return uiCommands_;
        }
        void QueueEvent(ScriptEvent event);
        const std::map<std::string, std::array<float, 3>>& Impulses() const
        {
            return impulses_;
        }
        /// <summary>成功した更新で予約された個体ごとのAnimatorパラメーターを取得します。</summary>
        const std::map<std::string, std::map<std::string, float>>& AnimatorParameters() const
        {
            return animatorParameters_;
        }
        const std::map<std::string, std::map<std::string, std::optional<AnimatorIkTarget>>>& IkTargets() const
        {
            return ikTargets_;
        }
        const std::map<std::string, std::optional<bool>>& RootMotions() const
        {
            return rootMotions_;
        }

    private:
        std::shared_ptr<const ScriptDefinitions> definitions_;
        std::shared_ptr<const Engine::ContentContext> content_;
        struct AdvanceOptions
        {
            const PhysicsWorld* physics;
            ScriptPhase phase;
            const SceneLayout* validatedBefore;
            bool startOnly;
        };
        /// <summary>入力と更新段階を指定し、候補のスクリプトを進めます。</summary>
        bool Advance(SceneLayout& layout, double seconds, std::string& error, const std::map<std::string, float>& input,
                     const std::map<std::string, bool>& pressed, const AdvanceOptions& options);
        bool pendingFixedCommands_ = false;
        struct PendingStart
        {
            std::function<void()> rollback;
            bool committed = false;
            /// <summary>未確定の開始処理が確保した資源を候補破棄時に解放します。</summary>
            ~PendingStart();
        };
        struct Instance
        {
            std::string owner, id, behaviour;
            std::map<std::string, float> state, parameters;
            ScriptValue::Object data, dataState;
            std::shared_ptr<PendingStart> pending;
        };
        struct PendingStop
        {
            std::shared_ptr<PendingStart> start;
            std::function<void()> callback;
        };
        std::vector<PendingStop> pendingStops_;
        /// <summary>終了時に必要な値を候補が所有し、失敗した候補からは呼び出しません。</summary>
        void QueueStop(ScenePlacement& owner, const Instance& instance);
        bool paused_ = false;
        struct TickContext
        {
            TickContext(const std::map<std::string, float>& values, const std::map<std::string, bool>& edges,
                        ScriptScene& candidate, ScriptPhase callbackPhase, double elapsed)
                : input(values), pressed(edges), scene(candidate), phase(callbackPhase), seconds(elapsed)
            {
            }
            const std::map<std::string, float>& input;
            const std::map<std::string, bool>& pressed;
            ScriptScene& scene;
            ScriptPhase phase;
            double seconds;
        };
        /// <summary>有効なスクリプトを実行し、継続するインスタンスの識別子を返します。</summary>
        std::set<std::pair<std::string, std::string>> TickLayout(SceneLayout& layout, const TickContext& tick,
                                                                 bool startOnly);
        Instance& GetInstance(ScenePlacement& object, const ScriptComponent& script, const ScriptDefinition& definition,
                              const TickContext& tick);
        void TickScript(ScenePlacement& object, const ScriptComponent& script, const ScriptDefinition& definition,
                        const TickContext& tick);
        void StopInactive(const std::set<std::pair<std::string, std::string>>& live, SceneLayout& layout);
        std::vector<ScenePlacement> CaptureStopOwners(const ScriptScene& scene) const;
        void StopDestroyed(const ScriptScene& scene, std::vector<ScenePlacement>& previousOwners);

        std::map<std::pair<std::string, std::string>, Instance> instances_;
        size_t nextId_ = 1;
        std::vector<ScriptEvent> events_;
        std::vector<SceneCommand> sceneCommands_;
        ScriptUiCommands uiCommands_;
        std::vector<AudioCommand> audioCommands_;
        std::map<std::string, std::array<float, 3>> impulses_;
        std::map<std::string, std::map<std::string, float>> animatorParameters_;
        std::map<std::string, std::map<std::string, std::optional<AnimatorIkTarget>>> ikTargets_;
        std::map<std::string, std::optional<bool>> rootMotions_;
    };
}
