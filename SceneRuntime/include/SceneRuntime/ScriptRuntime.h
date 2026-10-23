#pragma once
#include <SceneRuntime/SceneLayout.h>
#include <SceneRuntime/ObjectReferenceDomain.h>
#include <SceneRuntime/ScriptSchema.h>
#include <SceneRuntime/AudioCommand.h>
#include <Engine/Assets/ContentContext.h>
#include <SceneRuntime/PhysicsWorld.h>
#include <functional>
#include <map>
#include <set>
#include <cstdint>
#include <memory>

namespace SceneRuntime
{
    enum class ScriptPhase
    {
        Update,
        FixedUpdate,
        LateUpdate
    };
    struct SceneCommand
    {
        std::string scene;
        bool additive = false, unload = false;
    };
    struct ScriptUiCommands
    {
        std::map<std::string, float> values;
        std::map<std::string, std::string> texts;
    };
    struct ScriptEvent
    {
        std::string name, sender, target;
        float value = 0;
        std::optional<AnimatorEventOccurrence> animation;
        std::string text;
    };
    // Commands are applied after every callback; object pointers never survive a frame.
    class ScriptScene final
    {
    public:
        explicit ScriptScene(SceneLayout& layout, size_t& nextId, const PhysicsWorld* physics = nullptr,
                             const SceneLayout* validatedBefore = nullptr)
            : layout_(layout), nextId_(nextId), physics_(physics), validatedBefore_(validatedBefore)
        {
        }
        /// <summary>IDから現在のオブジェクトを参照します。参照はコールバック中だけ有効です。</summary>
        const ScenePlacement* Find(const std::string& id) const;
        /// <summary>指定したメンバーに対応する型付きComponentを取得します。</summary>
        template <class T> const T* GetComponent(const std::string& id, std::optional<T> ScenePlacement::* member) const
        {
            const auto* found = Find(id);
            return found && (found->*member) ? &*(found->*member) : nullptr;
        }
        /// <summary>同じ表示名を持つすべてのオブジェクトIDを取得します。</summary>
        std::vector<std::string> FindByName(const std::string& name) const;
        /// <summary>フレーム末尾で生成するオブジェクトを予約し、新しいIDを返します。</summary>
        std::string Spawn(ScenePlacement object);
        /// <summary>対象と子孫を親子・UI参照を維持して複製します。</summary>
        std::string Instantiate(const std::string& id, const std::array<float, 3>& position);
        /// <summary>対象と子孫の削除をフレーム末尾へ予約します。</summary>
        void Destroy(const std::string& id);
        /// <summary>Componentセットの置き換えをフレーム末尾へ予約します。</summary>
        void SetComponents(const std::string& id, const ScenePlacement& components);
        /// <summary>宛先付きイベントを次のScript更新へ予約します。</summary>
        void Emit(ScriptEvent event);
        /// <summary>成功したフレームでUIの数値bindingを設定します。</summary>
        void SetUiValue(const std::string& key, float value);
        /// <summary>成功したフレームでUIの文字列bindingを設定します。</summary>
        void SetUiText(const std::string& key, std::string text);
        /// <summary>成功した更新後に保存シーンのロードを予約します。</summary>
        void LoadScene(std::string scene, bool additive = false)
        {
            sceneCommands.push_back({std::move(scene), additive, false});
        }
        /// <summary>成功した更新後にロード済みシーンのアンロードを予約します。</summary>
        void UnloadScene(std::string scene)
        {
            sceneCommands.push_back({std::move(scene), false, true});
        }
        /// <summary>動的剛体へ適用するインパルスを次の物理更新へ予約します。</summary>
        void AddImpulse(const std::string& id, const std::array<float, 3>& impulse);
        /// <summary>個体のAnimatorパラメーター変更を予約します。骨格の時計や保存データを作り直しません。</summary>
        void SetAnimatorParameter(const std::string& id, const std::string& name, float value);
        /// <summary>ルート移動の実行時設定を変更します。nulloptは保存設定へ戻します。</summary>
        void SetRootMotion(const std::string& id, std::optional<bool> enabled);
        /// <summary>名前付きIK制約の実行時目標を予約します。時計・保存設定を保持します。</summary>
        void SetIkTarget(const std::string& id, const std::string& name, const AnimatorIkTarget& target);
        /// <summary>実行時目標を解除して保存設定へ戻します。</summary>
        void ClearIkTarget(const std::string& id, const std::string& name);
        /// <summary>実行中の物理ワールドから最も近いColliderの交点を取得します。</summary>
        std::optional<PhysicsRayHit> Raycast(const std::array<float, 3>& origin, const std::array<float, 3>& direction,
                                             float distance, unsigned int mask = 0xffffffffu, bool triggers = false,
                                             const std::string& ignore = {}) const
        {
            return physics_ ? physics_->Raycast(origin, direction, distance, mask, triggers, ignore) : std::nullopt;
        }
        /// <summary>予約した構造変更を検証し、候補シーンへ反映します。</summary>
        void Commit();
        /// <summary>音源の再生を予約します。コールバックと外側の更新が成功するまで再生しません。</summary>
        void PlayAudio(const std::string& object);
        /// <summary>名前付き音声キューの再生を、成功した更新の後へ予約します。</summary>
        void AudioCue(const std::string& cue);
        std::vector<ScriptEvent> events;
        std::vector<SceneCommand> sceneCommands;
        ScriptUiCommands uiCommands;
        std::vector<AudioCommand> audioCommands;
        std::string currentOwner;
        std::map<std::string, std::array<float, 3>> impulses;
        std::map<std::string, std::map<std::string, float>> animatorParameters;
        std::map<std::string, std::map<std::string, std::optional<AnimatorIkTarget>>> ikTargets;
        std::map<std::string, std::optional<bool>> rootMotions;

    private:
        void DestroyQueuedObjects();

        SceneLayout& layout_;
        size_t& nextId_;
        const PhysicsWorld* physics_;
        const SceneLayout* validatedBefore_;
        std::set<std::string> reservedIds_;
        std::vector<ScenePlacement> spawned_;
        std::set<std::string> destroyed_;
        std::map<std::string, ScenePlacement> components_;
    };
    struct ScriptContext
    {
        /// <summary>必須の参照と、その更新で有効な入力・シーン・イベントを初期化します。</summary>
        ScriptContext(ScenePlacement& owner, const std::map<std::string, float>& values,
                      std::map<std::string, float>& instanceState, double elapsed = 0,
                      const std::map<std::string, float>* actionValues = nullptr,
                      const std::map<std::string, bool>* actionPressed = nullptr, ScriptScene* sceneApi = nullptr,
                      const ScriptEvent* incomingEvent = nullptr)
            : object(owner), parameters(values), state(instanceState), seconds(elapsed), input(actionValues),
              pressed(actionPressed), scene(sceneApi), event(incomingEvent)
        {
            if (scene)
                scene->currentOwner = owner.id;
        }
        ScenePlacement& object;
        const std::map<std::string, float>& parameters;
        std::map<std::string, float>& state;
        double seconds = 0;
        const std::map<std::string, float>* input = nullptr;
        const std::map<std::string, bool>* pressed = nullptr;
        ScriptScene* scene = nullptr;
        const ScriptEvent* event = nullptr;
        const ScriptValue::Object* data = nullptr;
        ScriptValue::Object* dataState = nullptr;
        ScriptPhase phase = ScriptPhase::Update;
        /// <summary>DLLの配置・一時コピー先によらない、登録元ゲームの素材ルートです。</summary>
        std::filesystem::path contentRoot;
        std::shared_ptr<const Engine::ContentContext> content;
        /// <summary>保存された型付きパラメーターを参照します。存在しない名前はnullです。</summary>
        const ScriptValue* Data(const std::string& name) const;
        /// <summary>オブジェクト参照の現在の対象を取得します。未設定・削除済みはnullです。</summary>
        const ScenePlacement* Reference(const std::string& name) const;
        float Input(const std::string& name) const;
        bool Pressed(const std::string& name) const;
        float Value(const std::string& name, float fallback = 0) const;
    };
    struct ScriptStopContext
    {
        /// <summary>終了確定では読み取り専用の所有者と実行状態だけを渡します。</summary>
        explicit ScriptStopContext(const ScriptContext& context)
            : object(context.object), parameters(context.parameters), state(context.state), data(context.data),
              dataState(context.dataState), contentRoot(context.contentRoot), content(context.content)
        {
        }
        const ScenePlacement& object;
        const std::map<std::string, float>& parameters;
        const std::map<std::string, float>& state;
        const ScriptValue::Object* data;
        const ScriptValue::Object* dataState;
        std::filesystem::path contentRoot;
        std::shared_ptr<const Engine::ContentContext> content;
    };
    struct ScriptDefinition : ScriptSchema
    {
        std::function<void(ScriptContext&)> start, update;
        /// <summary>副作用を起こさず候補所有者だけを変更します。失敗時は候補ごと破棄します。</summary>
        std::function<void(ScriptContext&)> prepareStop;
        /// <summary>成功確定後、または開始失敗の後始末で外部資源を解放します。配置は変更できません。</summary>
        std::function<void(const ScriptStopContext&)> stop;
        std::function<void(ScriptContext&)> onEvent;
        std::function<void(ScriptContext&)> fixedUpdate, lateUpdate;
        /// <summary>ホストの一時停止・フォーカス変更を外部資源へ反映します。</summary>
        std::function<void(ScriptContext&, bool)> pause;
    };
    class ScriptRegistry final
    {
    public:
        // Register before Play, on the main thread. Duplicate names and invalid metadata are refused.
        static bool Register(std::string name, ScriptDefinition definition);
        /// <summary>再生前にモジュール所有の処理を一括置換します。失敗時は既存の登録を保持します。</summary>
        static bool InstallModule(const std::string& owner, std::map<std::string, ScriptDefinition> definitions,
                                  std::string& error);
        static const std::map<std::string, ScriptDefinition>& Definitions();
        /// <summary>登録とモジュール交換の成功時に進む定義世代を取得します。</summary>
        static std::uint64_t Revision();
    };
    class ScriptRuntime final
    {
    public:
        /// <summary>全ライフサイクルのコールバックへ所有者の参照情報を渡します。</summary>
        void SetContentContext(std::shared_ptr<const Engine::ContentContext> content)
        {
            content_ = std::move(content);
        }
        /// <summary>有効なスクリプトの登録と保存値を、開始処理を呼ばずに検証します。</summary>
        static bool ValidateForPlay(const SceneLayout& layout, std::string& error);
        /// <summary>外側の候補上で開始処理だけを実行し、通常更新と時計を進めません。</summary>
        bool StartCandidate(SceneLayout& layout, std::string& error);
        // Transactional update. Use context.scene for structural changes; never resize arrays in a callback.
        bool Update(SceneLayout& layout, double seconds, std::string& error,
                    const std::map<std::string, float>& input = {}, const std::map<std::string, bool>& pressed = {},
                    const PhysicsWorld* physics = nullptr, ScriptPhase phase = ScriptPhase::Update);
        /// <summary>外側で配置・実行状態・物理の取引候補を所有する更新を実行します。検証済みの変更前配置は差分検証に使用し、失敗時は全候補を破棄してください。</summary>
        bool UpdateCandidate(SceneLayout& layout, double seconds, std::string& error,
                             const std::map<std::string, float>& input = {},
                             const std::map<std::string, bool>& pressed = {}, const PhysicsWorld* physics = nullptr,
                             ScriptPhase phase = ScriptPhase::Update, const SceneLayout* validatedBefore = nullptr);
        /// <summary>指定した更新段階を持つ有効な処理があるか調べます。</summary>
        static bool HasPhase(const SceneLayout& layout, ScriptPhase phase);
        /// <summary>外側の更新成功後に開始処理を確定し、予約した終了処理を一度だけ呼びます。</summary>
        void CommitStarts() noexcept;
        /// <summary>実行中のScriptへ一時停止と再開を通知します。</summary>
        void Pause(SceneLayout& layout, bool paused) noexcept;
        void Stop(SceneLayout& layout) noexcept;
        /// <summary>取り除かれた個体の終了処理を旧配置で実行し、残る個体の実行状態を維持します。</summary>
        void StopRemoved(SceneLayout& previous, const SceneLayout& next) noexcept;
        /// <summary>遷移で消える処理を候補から外し、終了処理は外側の確定まで予約します。</summary>
        void PrepareTransition(const SceneLayout& previous, SceneLayout& next);
        /// <summary>成功したフレームで予約されたシーン操作を取得します。</summary>
        std::vector<SceneCommand> TakeSceneCommands()
        {
            auto commands = std::move(sceneCommands_);
            sceneCommands_.clear();
            return commands;
        }
        /// <summary>成功したFixed・Update・LateUpdateが予約したUI変更を一度だけ取得します。</summary>
        ScriptUiCommands TakeUiCommands()
        {
            auto commands = std::move(uiCommands_);
            uiCommands_ = {};
            return commands;
        }
        /// <summary>成功した更新の音声指示を一度だけ取り出します。</summary>
        std::vector<AudioCommand> TakeAudioCommands()
        {
            auto commands = std::move(audioCommands_);
            audioCommands_.clear();
            return commands;
        }
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
        std::shared_ptr<const Engine::ContentContext> content_;
        bool Advance(SceneLayout& layout, double seconds, std::string& error, const std::map<std::string, float>& input,
                     const std::map<std::string, bool>& pressed, const PhysicsWorld* physics, ScriptPhase phase,
                     const SceneLayout* validatedBefore, bool startOnly = false);
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
