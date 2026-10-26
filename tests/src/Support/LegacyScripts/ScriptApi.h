#pragma once
#include <SceneRuntime/Scene/SceneLayout.h>
#include <SceneRuntime/Scene/WorldReadView.h>
#include <SceneRuntime/Scene/ObjectReferenceDomain.h>
#include <SceneRuntime/Scripts/ScriptSchema.h>
#include <SceneRuntime/Scripts/ScriptSignals.h>
#include <SceneRuntime/Audio/AudioCommand.h>
#include <SceneRuntime/Physics/PhysicsTypes.h>
#include <functional>
#include <map>
#include <set>
#include <cstdint>
#include <memory>

namespace Engine
{
    class ContentContext;
}

namespace SceneRuntime
{
    class PhysicsWorld;
    class RuntimeScriptScene;

    // 構造変更は各更新段階のコールバックを終えてから候補へ反映します。オブジェクトへの参照はコールバックの外へ持ち越さないでください。
    class ScriptScene final
    {
    public:
        /// <summary>更新候補と ID 発行元を参照します。参照先はこの API の使用中保持してください。</summary>
        explicit ScriptScene(SceneLayout& layout, size_t& nextId, const PhysicsWorld* physics = nullptr,
                             const SceneLayout* validatedBefore = nullptr)
            : layout_(layout), nextId_(nextId), physics_(physics), validatedBefore_(validatedBefore)
        {
        }
        /// <summary>IDから現在のオブジェクトを参照します。参照はコールバック中だけ有効です。</summary>
        const ScenePlacement* Find(const std::string& id) const;
        /// <summary>コールバック中の候補を読み取るビューを取得します。段階をまたいで保持しないでください。</summary>
        WorldReadView Read() const noexcept;
        /// <summary>指定したメンバーに対応する型付きコンポーネントを取得します。</summary>
        template <class T> const T* GetComponent(const std::string& id, std::optional<T> ScenePlacement::* member) const
        {
            const auto* found = Find(id);
            return found && (found->*member) ? &*(found->*member) : nullptr;
        }
        /// <summary>同じ表示名を持つすべてのオブジェクトIDを取得します。</summary>
        std::vector<std::string> FindByName(const std::string& name) const;
        /// <summary>現在の更新段階の終了時に候補へ追加するオブジェクトを予約し、新しい ID を返します。</summary>
        std::string Spawn(ScenePlacement object);
        /// <summary>対象と子孫の複製を予約し、複製内の親子・コンポーネント参照を新しい ID へ付け替えます。</summary>
        std::string Instantiate(const std::string& id, const std::array<float, 3>& position);
        /// <summary>対象と子孫の削除を、現在の更新段階の終了時へ予約します。</summary>
        void Destroy(const std::string& id);
        /// <summary>コンポーネント構成の置き換えを、現在の更新段階の終了時へ予約します。</summary>
        void SetComponents(const std::string& id, const ScenePlacement& components);
        /// <summary>段階の終了時にワールド姿勢を保って親を変更します。構造の不正は確定前に拒否します。</summary>
        void SetParent(std::string id, std::string parent);
        /// <summary>宛先付きイベントを次のスクリプト更新へ予約します。</summary>
        void Emit(ScriptEvent event);
        /// <summary>成功したフレームで UI の数値バインディングを設定します。</summary>
        void SetUiValue(const std::string& key, float value);
        /// <summary>成功したフレームで UI の文字列バインディングを設定します。</summary>
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
                                             const std::string& ignore = {}) const;
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
        friend struct ScriptContext;
        friend class LegacyScriptAdapter;
        RuntimeScriptScene* runtime_ = nullptr;
        ScenePlacement* runtimeOwner_ = nullptr;
        std::optional<WorldReadView> runtimeRead_;
        mutable std::map<std::string, ScenePlacement> runtimeObjects_;
        /// <summary>同じ段階で予約済みのコンポーネント変更を含む値を取得します。</summary>
        ScenePlacement ComponentsFor(const std::string& id) const;
        /// <summary>削除したAnimatorに対する更新要求を破棄します。</summary>
        void PruneAnimatorCommands();
        /// <summary>削除予約に対象の子孫を含め、候補のオブジェクト配列からまとめて取り除きます。</summary>
        void DestroyQueuedObjects();

        SceneLayout& layout_;
        size_t& nextId_;
        const PhysicsWorld* physics_;
        const SceneLayout* validatedBefore_;
        std::set<std::string> reservedIds_;
        std::vector<ScenePlacement> spawned_;
        std::set<std::string> destroyed_;
        std::map<std::string, ScenePlacement> components_;
        std::map<std::string, std::string> parents_;
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
        /// <summary>現在の入力値を取得します。入力や名前が未設定の場合は 0 です。</summary>
        float Input(const std::string& name) const;
        /// <summary>今回の更新で入力が押されたかを取得します。未設定の場合は false です。</summary>
        bool Pressed(const std::string& name) const;
        /// <summary>数値パラメーターを取得します。名前が存在しない場合は fallback を返します。</summary>
        float Value(const std::string& name, float fallback = 0) const;
        /// <summary>所有個体の姿勢を候補へ反映し、未確定の変更結果を返します。通知の公開は外側の成功後です。</summary>
        SceneEditResult SetLocalTransform(const LocalTransform& transform);
        /// <summary>所有個体のコンポーネント変更を段階末へ予約し、未確定の変更結果を返します。</summary>
        SceneEditResult SetComponents(const ScenePlacement& components);
        /// <summary>一つの型付きコンポーネント変更を段階末へ予約します。</summary>
        template <class T>
        SceneEditResult SetComponent(std::optional<T> ScenePlacement::* member, std::optional<T> component)
        {
            auto candidate = scene ? scene->ComponentsFor(object.id) : object;
            candidate.*member = std::move(component);
            return SetComponents(candidate);
        }
        /// <summary>所有個体の親変更を段階末へ予約します。</summary>
        void SetParent(std::string parent);
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
    struct ScriptCallbacks
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
    struct ScriptDefinition : ScriptSchema, ScriptCallbacks
    {
    };
}
