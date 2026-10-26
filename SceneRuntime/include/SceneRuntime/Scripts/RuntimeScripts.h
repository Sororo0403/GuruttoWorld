#pragma once
#include <SceneRuntime/Scene/WorldState.h>
#include <SceneRuntime/Scripts/ScriptSchema.h>
#include <SceneRuntime/Scripts/ScriptSignals.h>
#include <SceneRuntime/Audio/AudioCommand.h>
#include <SceneRuntime/Physics/PhysicsTypes.h>
#include <functional>

namespace Engine
{
    class ContentContext;
}

namespace SceneRuntime
{
    class RuntimeScriptScene;

    struct RuntimeScriptContext
    {
        /// <summary>一つのコールバックの所有者と、候補上の状態・読み取り値を借用します。</summary>
        RuntimeScriptContext(EntityHandle owner, const std::map<std::string, float>& parameters,
                             std::map<std::string, float>& state, const ScriptValue::Object& data,
                             ScriptValue::Object& dataState, RuntimeScriptScene& scene);
        EntityHandle entity;
        const std::map<std::string, float>& parameters;
        std::map<std::string, float>& state;
        const ScriptValue::Object& data;
        ScriptValue::Object& dataState;
        RuntimeScriptScene& scene;
        double seconds = 0;
        ScriptPhase phase = ScriptPhase::Update;
        const ScriptEvent* event = nullptr;
        std::filesystem::path contentRoot;
        std::shared_ptr<const Engine::ContentContext> content;

        /// <summary>この段階の書き込み候補を読むビューを取得します。</summary>
        WorldState::ReadView Read() const noexcept;
        /// <summary>型付きパラメーターを参照し、存在しない場合はnullを返します。</summary>
        const ScriptValue* Data(const std::string& name) const;
        /// <summary>保存IDの参照を現在の実行ハンドルへ解決します。</summary>
        std::optional<EntityHandle> Reference(const std::string& name) const;
        /// <summary>数値パラメーター、または指定した既定値を返します。</summary>
        float Value(const std::string& name, float fallback = 0) const;
        /// <summary>今回の入力値を返します。未設定の場合は0です。</summary>
        float Input(const std::string& name) const;
        /// <summary>今回の入力の押下を返します。未設定の場合はfalseです。</summary>
        bool Pressed(const std::string& name) const;
        /// <summary>姿勢を候補へ反映します。公開は外側のフレーム成功後です。</summary>
        SceneEditResult SetLocalTransform(const LocalTransform& value);
        /// <summary>コンポーネントの値を段階末へ予約します。</summary>
        template <class T> SceneEditResult SetComponent(std::optional<T> value);
        /// <summary>同じ型を複数持つコンポーネントの一覧を、段階末へ予約します。</summary>
        template <class T> SceneEditResult SetRepeated(std::vector<T> value);
        /// <summary>親の変更を段階末へ予約します。未設定のハンドルはルートを表します。</summary>
        void SetParent(EntityHandle parent);

    private:
        friend class RuntimeScripts;
        friend class LegacyScriptAdapter;
        const std::map<std::string, float>* input_ = nullptr;
        const std::map<std::string, bool>* pressed_ = nullptr;
        ScenePlacement* retiredOwner_ = nullptr;
    };

    struct RuntimeScriptStopContext
    {
        EntityHandle entity;
        ScenePlacement object;
        std::map<std::string, float> parameters, state;
        ScriptValue::Object data, dataState;
        std::filesystem::path contentRoot;
        std::shared_ptr<const Engine::ContentContext> content;
    };
    struct RuntimeScriptDefinition : ScriptSchema
    {
        std::function<void(RuntimeScriptContext&)> start, update, fixedUpdate, lateUpdate, onEvent, prepareStop;
        std::function<void(const RuntimeScriptStopContext&)> stop;
        std::function<void(RuntimeScriptContext&, bool)> pause;
    };
    using RuntimeScriptDefinitions = std::map<std::string, RuntimeScriptDefinition>;

    class RuntimeScriptRegistry final
    {
    public:
        /// <summary>再生前に名前と宣言を検証して登録します。</summary>
        static bool Register(std::string name, RuntimeScriptDefinition definition);
        /// <summary>セッションが保持する不変の定義世代を取得します。</summary>
        static std::shared_ptr<const RuntimeScriptDefinitions> Capture();
        /// <summary>登録とモジュール交換に成功した定義世代を取得します。</summary>
        static std::uint64_t Revision();
        /// <summary>モジュール単位の定義を一括交換し、失敗時は旧登録を保持します。</summary>
        static bool InstallModule(const std::string& owner, RuntimeScriptDefinitions definitions, std::string& error);
    };

    struct RuntimeScriptEffects
    {
        struct Impulse
        {
            EntityHandle entity;
            std::array<float, 3> value{};
            ComponentInstanceHandle body;
            /// <summary>予約した剛体世代と力積を比較します。</summary>
            bool operator==(const Impulse&) const = default;
        };
        std::vector<SceneCommand> scenes;
        ScriptUiCommands ui;
        std::vector<AudioCommand> audio;
        std::vector<Impulse> impulses;
        std::map<std::string, std::map<std::string, float>> animatorParameters;
        std::map<std::string, std::vector<std::string>> clearAnimatorParameters;
        std::map<std::string, std::map<std::string, std::optional<AnimatorIkTarget>>> ikTargets;
        std::map<std::string, std::optional<bool>> rootMotions;
        /// <summary>確保を行わず、準備済みの副作用予約を交換します。</summary>
        void Swap(RuntimeScriptEffects& other) noexcept;
        /// <summary>予約した値を、確保せずに破棄します。</summary>
        void Clear() noexcept;
    };

    class RuntimeScriptScene final
    {
        struct Impl;

    public:
        /// <summary>この段階の候補を読むビューを取得します。</summary>
        WorldState::ReadView Read() const noexcept;
        /// <summary>保存IDを実行ハンドルへ解決します。</summary>
        std::optional<EntityHandle> Find(std::string_view id) const;
        /// <summary>同じ表示名を持つ生存個体を取得します。</summary>
        std::vector<EntityHandle> FindByName(std::string_view name) const;
        /// <summary>段階末に追加する定義を予約し、保存IDを返します。</summary>
        std::string Spawn(ScenePlacement value);
        /// <summary>対象と子孫を段階末に複製し、参照を付け替えたルートの保存IDを返します。</summary>
        std::string Instantiate(EntityHandle entity, const std::array<float, 3>& position);
        /// <summary>対象と子孫の削除を段階末へ予約します。</summary>
        void Destroy(EntityHandle entity);
        /// <summary>親の変更を段階末へ予約します。</summary>
        void SetParent(EntityHandle entity, EntityHandle parent);
        /// <summary>型付きコンポーネントを段階末へ予約します。</summary>
        template <class T> SceneEditResult SetComponent(EntityHandle entity, std::optional<T> value)
        {
            return SetValue(entity, RuntimeComponentValue{std::move(value)});
        }
        /// <summary>同じ型を複数持つコンポーネントの一覧を、段階末へ予約します。</summary>
        template <class T> SceneEditResult SetRepeated(EntityHandle entity, std::vector<T> value)
        {
            return SetValue(entity, RuntimeComponentValue{std::move(value)});
        }
        /// <summary>次の可変更新へイベントを予約します。</summary>
        void Emit(ScriptEvent event);
        /// <summary>確定後のシーン操作を予約します。</summary>
        void LoadScene(std::string scene, bool additive = false);
        /// <summary>確定後のシーンの解除を予約します。</summary>
        void UnloadScene(std::string scene);
        /// <summary>確定後のUI数値を予約します。</summary>
        void SetUiValue(std::string key, float value);
        /// <summary>確定後のUI文字列を予約します。</summary>
        void SetUiText(std::string key, std::string text);
        /// <summary>確定後の音声指示を予約します。</summary>
        void PlayAudio(const std::string& object);
        /// <summary>確定後の共通音声の指示を予約します。</summary>
        void AudioCue(const std::string& cue);
        /// <summary>物理段階へ力積を予約します。</summary>
        void AddImpulse(EntityHandle entity, const std::array<float, 3>& value);
        /// <summary>Animatorのパラメーターを予約します。</summary>
        void SetAnimatorParameter(EntityHandle entity, std::string name, float value);
        /// <summary>実行時パラメーターを解除し、入力または保存された設定に戻します。</summary>
        void ClearAnimatorParameter(EntityHandle entity, std::string name);
        /// <summary>AnimatorのIK上書きを予約します。</summary>
        void SetIkTarget(EntityHandle entity, std::string name, std::optional<AnimatorIkTarget> value);
        /// <summary>ルート移動の上書きを予約します。</summary>
        void SetRootMotion(EntityHandle entity, std::optional<bool> enabled);
        /// <summary>入力されたワールド空間の光線に対する、物理側の問い合わせ結果を取得します。</summary>
        std::optional<PhysicsRayHit> Raycast(const std::array<float, 3>& origin, const std::array<float, 3>& direction,
                                             float distance, unsigned int mask = 0xffffffffu, bool triggers = false,
                                             const std::string& ignore = {}) const;

    private:
        friend class RuntimeScripts;
        friend struct RuntimeScriptContext;
        friend class ScriptScene;
        friend class LegacyScriptAdapter;
        /// <summary>移行中のSDKが、予約済みの値も含む一個体だけを取得します。</summary>
        ScenePlacement LegacyComponents(EntityHandle entity) const;
        /// <summary>移行中のSDKの全コンポーネント要求を、型別の予約へ変換します。</summary>
        void SetLegacyComponents(EntityHandle entity, const ScenePlacement& value);
        /// <summary>旧コールバックが変更した一個体を、保存型を正本にせず型別の候補へ取り込みます。</summary>
        void ApplyLegacyObject(EntityHandle entity, const ScenePlacement& before, const ScenePlacement& after);
        /// <summary>型付きの変更値を予約します。</summary>
        SceneEditResult SetValue(EntityHandle entity, RuntimeComponentValue value);
        /// <summary>一つの実行段階だけが持つ要求先を借用します。</summary>
        explicit RuntimeScriptScene(Impl& impl) noexcept;
        Impl* impl_;
    };

    template <class T> SceneEditResult RuntimeScriptContext::SetComponent(std::optional<T> value)
    {
        return scene.SetComponent(entity, std::move(value));
    }
    template <class T> SceneEditResult RuntimeScriptContext::SetRepeated(std::vector<T> value)
    {
        return scene.SetRepeated(entity, std::move(value));
    }

    struct RuntimeScriptQueries
    {
        std::function<std::optional<PhysicsRayHit>(const std::array<float, 3>&, const std::array<float, 3>&, float,
                                                   unsigned int, bool, const std::string&)>
            raycast;
    };

    class RuntimeScripts final
    {
        struct Impl;

    public:
        class Frame final
        {
            struct Pending;

        public:
            /// <summary>未確定の開始処理を片付け、候補の状態と副作用予約を破棄します。</summary>
            ~Frame();
            /// <summary>取引の所有権を複製しません。</summary>
            Frame(const Frame&) = delete;
            /// <summary>取引の所有権を代入しません。</summary>
            Frame& operator=(const Frame&) = delete;
            /// <summary>有効なインスタンスの開始だけを候補で実行します。</summary>
            bool Start(std::string& error);
            /// <summary>終了処理に必要な所有者の値を保持してから、明示的な構造変更を候補へ適用します。</summary>
            bool Edit(const SceneEdit& edit, std::string& error);
            /// <summary>時計で指定した3D・演出プロパティを、型付きの候補値へ反映します。</summary>
            bool Animate(const std::map<std::string, float>& clocks, std::string& error, bool& changed);
            /// <summary>指定した段階を候補で進め、段階末に構造とコンポーネント予約を反映します。</summary>
            bool Run(ScriptPhase phase, double seconds, std::string& error);
            /// <summary>候補内に指定した段階の有効なコールバックがあるかを調べます。</summary>
            bool HasPhase(ScriptPhase phase) const;
            /// <summary>Pauseまたは再開のコールバックを候補で実行します。</summary>
            bool Pause(bool paused, std::string& error);
            /// <summary>固定段階と可変段階がそれぞれ使用する入力候補を設定します。</summary>
            void SetInput(const std::map<std::string, float>& input, const std::map<std::string, bool>& pressed);
            /// <summary>同じフレームの物理・Animationが発行したイベントを、次の可変更新へ予約します。</summary>
            bool QueueEvent(ScriptEvent event, std::string& error);
            /// <summary>ホストが明示した操作も、Scriptと同じ型付き検証・確定境界へ渡します。</summary>
            bool ApplyEffects(const RuntimeScriptEffects& effects, std::string& error);
            /// <summary>公開する状態表と通知を確保し、確定後の終了処理を準備します。</summary>
            bool Prepare(std::string& error);
            /// <summary>準備済みの状態と予約を、確保せずに公開します。</summary>
            bool Commit() noexcept;
            /// <summary>外側の状態確定後に終了コールバックを一度だけ実行します。</summary>
            void FinishCallbacks() noexcept;
            /// <summary>直前の段階が予約した実行指示を取得します。</summary>
            const RuntimeScriptEffects& Effects() const noexcept;
            /// <summary>実行状態の確定前後で公開する通知を取得します。</summary>
            const SceneChangeSet& Changes() const noexcept;

        private:
            friend class RuntimeScripts;
            friend class LegacyScriptAdapter;
            /// <summary>互換検証が次の段階へ渡すため、候補の予約イベントを借用します。</summary>
            const std::vector<ScriptEvent>& PendingEvents() const noexcept;
            /// <summary>Worldの候補と一緒に進める実行状態の候補を作ります。</summary>
            Frame(RuntimeScripts& owner, WorldState::Transaction& transaction,
                  const std::map<std::string, float>& input, const std::map<std::string, bool>& pressed,
                  RuntimeScriptQueries queries);
            std::unique_ptr<Pending> pending_;
        };

        /// <summary>セッションの開始時に不変のScript定義を固定します。</summary>
        explicit RuntimeScripts(
            std::shared_ptr<const RuntimeScriptDefinitions> definitions = RuntimeScriptRegistry::Capture());
        /// <summary>Scriptの内部データを解放します。停止コールバックはStopで先に実行してください。</summary>
        ~RuntimeScripts();
        /// <summary>実行状態の暗黙な複製を禁止します。</summary>
        RuntimeScripts(const RuntimeScripts&) = delete;
        /// <summary>実行状態の暗黙な代入を禁止します。</summary>
        RuntimeScripts& operator=(const RuntimeScripts&) = delete;
        /// <summary>一つのWorld取引と入力を借用して、フレーム候補を作ります。</summary>
        Frame BeginFrame(WorldState::Transaction& transaction, const std::map<std::string, float>& input = {},
                         const std::map<std::string, bool>& pressed = {}, RuntimeScriptQueries queries = {});
        /// <summary>実行できる宣言とパラメーターかを、開始処理なしで検証します。</summary>
        bool Validate(WorldState::ReadView view, std::string& error) const;
        /// <summary>次の可変更新へイベントを予約します。準備中の再入は拒否します。</summary>
        bool QueueEvent(ScriptEvent event, std::string& error);
        /// <summary>外側の確定後に、副作用予約を一度だけ取得します。</summary>
        RuntimeScriptEffects TakeEffects();
        /// <summary>事前に構築した受取先へ予約を交換します。準備中は拒否し、確保を行いません。</summary>
        bool DrainEffects(RuntimeScriptEffects& target) noexcept;
        /// <summary>現在の読取元に、指定した段階の有効なコールバックがあるかを返します。</summary>
        bool HasPhase(WorldState::ReadView view, ScriptPhase phase) const;
        /// <summary>確定済みのPause状態を取得します。</summary>
        bool Paused() const noexcept;
        /// <summary>このセッションが使用する参照元と素材ルートを設定します。</summary>
        void SetContent(std::shared_ptr<const Engine::ContentContext> content, std::filesystem::path root);
        /// <summary>終了準備と終了処理を実行し、全インスタンスを停止します。</summary>
        void Stop(WorldState& world) noexcept;

    private:
        std::unique_ptr<Impl> impl_;
    };
}
