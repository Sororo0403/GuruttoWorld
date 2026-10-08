#pragma once
#include <SceneRuntime/SceneLayout.h>
#include <SceneRuntime/PhysicsWorld.h>
#include <functional>
#include <map>
#include <set>

namespace SceneRuntime
{
    struct ScriptEvent
    {
        std::string name,sender,target;
        float value=0;
        std::optional<AnimatorEventOccurrence> animation;
    };
    // Commands are applied after every callback; object pointers never survive a frame.
    class ScriptScene final
    {
    public:
        explicit ScriptScene(SceneLayout& layout,size_t& nextId,const PhysicsWorld* physics=nullptr) : layout_(layout),nextId_(nextId),physics_(physics) {}
        /// <summary>IDから現在のオブジェクトを参照します。参照はコールバック中だけ有効です。</summary>
        const ScenePlacement* Find(const std::string& id) const;
        /// <summary>指定したメンバーに対応する型付きComponentを取得します。</summary>
        template<class T> const T* GetComponent(const std::string& id,std::optional<T> ScenePlacement::* member) const
        {
            const auto* found=Find(id);
            return found && (found->*member) ? &*(found->*member) : nullptr;
        }
        /// <summary>同じ表示名を持つすべてのオブジェクトIDを取得します。</summary>
        std::vector<std::string> FindByName(const std::string& name) const;
        /// <summary>フレーム末尾で生成するオブジェクトを予約し、新しいIDを返します。</summary>
        std::string Spawn(ScenePlacement object);
        /// <summary>対象と子孫を親子・UI参照を維持して複製します。</summary>
        std::string Instantiate(const std::string& id,const std::array<float,3>& position);
        /// <summary>対象と子孫の削除をフレーム末尾へ予約します。</summary>
        void Destroy(const std::string& id);
        /// <summary>Componentセットの置き換えをフレーム末尾へ予約します。</summary>
        void SetComponents(const std::string& id,const ScenePlacement& components);
        /// <summary>宛先付きイベントを次のScript更新へ予約します。</summary>
        void Emit(ScriptEvent event);
        /// <summary>動的剛体へ適用するインパルスを次の物理更新へ予約します。</summary>
        void AddImpulse(const std::string& id,const std::array<float,3>& impulse);
        /// <summary>個体のAnimatorパラメーター変更を予約します。骨格の時計や保存データを作り直しません。</summary>
        void SetAnimatorParameter(const std::string& id,const std::string& name,float value);
        /// <summary>ルート移動の実行時設定を変更します。nulloptは保存設定へ戻します。</summary>
        void SetRootMotion(const std::string& id,std::optional<bool> enabled);
        /// <summary>名前付きIK制約の実行時目標を予約します。時計・保存設定を保持します。</summary>
        void SetIkTarget(const std::string& id,const std::string& name,const AnimatorIkTarget& target);
        /// <summary>実行時目標を解除して保存設定へ戻します。</summary>
        void ClearIkTarget(const std::string& id,const std::string& name);
        /// <summary>実行中の物理ワールドから最も近いColliderの交点を取得します。</summary>
        std::optional<PhysicsRayHit> Raycast(const std::array<float,3>& origin,const std::array<float,3>& direction,float distance,
            unsigned int mask=0xffffffffu,bool triggers=false,const std::string& ignore={}) const
        { return physics_ ? physics_->Raycast(origin,direction,distance,mask,triggers,ignore) : std::nullopt; }
        /// <summary>予約した構造変更を検証し、候補シーンへ反映します。</summary>
        void Commit();
        std::vector<ScriptEvent> events;
        std::map<std::string,std::array<float,3>> impulses;
        std::map<std::string,std::map<std::string,float>> animatorParameters;
        std::map<std::string,std::map<std::string,std::optional<AnimatorIkTarget>>> ikTargets;
        std::map<std::string,std::optional<bool>> rootMotions;
    private:
        SceneLayout& layout_;
        size_t& nextId_;
        const PhysicsWorld* physics_;
        std::vector<ScenePlacement> spawned_;
        std::set<std::string> destroyed_;
        std::map<std::string,ScenePlacement> components_;
    };
    struct ScriptField { float initial=0,minimum=-1000000,maximum=1000000; };
    struct ScriptContext
    {
        /// <summary>必須の参照と、その更新で有効な入力・シーン・イベントを初期化します。</summary>
        ScriptContext(ScenePlacement& owner,const std::map<std::string,float>& values,std::map<std::string,float>& instanceState,
            double elapsed=0,const std::map<std::string,float>* actionValues=nullptr,const std::map<std::string,bool>* actionPressed=nullptr,
            ScriptScene* sceneApi=nullptr,const ScriptEvent* incomingEvent=nullptr) :
            object(owner),parameters(values),state(instanceState),seconds(elapsed),input(actionValues),pressed(actionPressed),scene(sceneApi),event(incomingEvent) {}
        ScenePlacement& object;
        const std::map<std::string,float>& parameters;
        std::map<std::string,float>& state;
        double seconds=0;
        const std::map<std::string,float>* input=nullptr;
        const std::map<std::string,bool>* pressed=nullptr;
        ScriptScene* scene=nullptr;
        const ScriptEvent* event=nullptr;
        float Input(const std::string& name) const;
        bool Pressed(const std::string& name) const;
        float Value(const std::string& name,float fallback=0) const;
    };
    struct ScriptDefinition
    {
        std::map<std::string,ScriptField> fields;
        std::function<void(ScriptContext&)> start,update,stop;
        std::function<void(ScriptContext&)> onEvent;
    };
    class ScriptRegistry final
    {
    public:
        // Register before Play, on the main thread. Duplicate names and invalid metadata are refused.
        static bool Register(std::string name,ScriptDefinition definition);
        static const std::map<std::string,ScriptDefinition>& Definitions();
    };
    class ScriptRuntime final
    {
    public:
        // Transactional update. Use context.scene for structural changes; never resize arrays in a callback.
        bool Update(SceneLayout& layout,double seconds,std::string& error,
            const std::map<std::string,float>& input={},const std::map<std::string,bool>& pressed={},const PhysicsWorld* physics=nullptr);
        void Stop(SceneLayout& layout) noexcept;
        void QueueEvent(ScriptEvent event);
        const std::map<std::string,std::array<float,3>>& Impulses() const { return impulses_; }
        /// <summary>成功した更新で予約された個体ごとのAnimatorパラメーターを取得します。</summary>
        const std::map<std::string,std::map<std::string,float>>& AnimatorParameters() const { return animatorParameters_; }
        const std::map<std::string,std::map<std::string,std::optional<AnimatorIkTarget>>>& IkTargets() const { return ikTargets_; }
        const std::map<std::string,std::optional<bool>>& RootMotions() const { return rootMotions_; }
    private:
        bool Advance(SceneLayout& layout,double seconds,std::string& error,
            const std::map<std::string,float>& input,const std::map<std::string,bool>& pressed,const PhysicsWorld* physics);
        struct Instance { std::string owner,id,behaviour; std::map<std::string,float> state,parameters; };
        std::map<std::pair<std::string,std::string>,Instance> instances_;
        size_t nextId_=1;
        std::vector<ScriptEvent> events_;
        std::map<std::string,std::array<float,3>> impulses_;
        std::map<std::string,std::map<std::string,float>> animatorParameters_;
        std::map<std::string,std::map<std::string,std::optional<AnimatorIkTarget>>> ikTargets_;
        std::map<std::string,std::optional<bool>> rootMotions_;
    };
}
