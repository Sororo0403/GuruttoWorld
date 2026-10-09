#pragma once
#include <SceneRuntime/ScenePhysics.h>
#include <memory>

namespace SceneRuntime
{
    struct PhysicsRayHit
    {
        std::string object;
        float distance=0;
        std::array<float,3> position{},normal{};
        bool trigger=false;
    };
    struct PhysicsContactEvent { std::string first,second,phase; bool trigger=false; };
    class PhysicsWorld final
    {
    public:
        class Transaction final
        {
        public:
            /// <summary>未確定の物理更新を巻き戻します。</summary>
            ~Transaction();
            /// <summary>巻き戻しの責任を移動します。</summary>
            Transaction(Transaction&& other) noexcept;
            Transaction(const Transaction&)=delete;
            Transaction& operator=(const Transaction&)=delete;
            /// <summary>物理更新を確定し、巻き戻しを解除します。</summary>
            void Commit() noexcept;
        private:
            friend class PhysicsWorld;
            struct State;
            /// <summary>物理ワールド・接触・キャラクター・予約インパルスを保存します。</summary>
            explicit Transaction(PhysicsWorld& world);
            PhysicsWorld* world_;
            std::unique_ptr<State> state_;
        };
        /// <summary>複数固定更新とリソース準備を一括で確定できる保存点を作ります。</summary>
        Transaction BeginTransaction();
        /// <summary>共有の物理型を登録し、実行用ワールドを準備します。</summary>
        PhysicsWorld();
        ~PhysicsWorld();
        PhysicsWorld(const PhysicsWorld&)=delete;
        PhysicsWorld& operator=(const PhysicsWorld&)=delete;
        /// <summary>入力と物理を更新し、成功した場合だけ配置・速度・接触を反映します。</summary>
        bool Advance(SceneLayout& layout,ScenePhysics::States& states,double seconds,float horizontal,float vertical,bool jump,
            const std::filesystem::path& root,std::string& error);
        /// <summary>次回の物理更新で適用するワールド空間のインパルスを予約します。</summary>
        bool AddImpulse(const std::string& object,const std::array<float,3>& impulse);
        /// <summary>ルート移動を一時的な衝突ワールドで制限します。物理時計・速度・接触履歴は変更しません。</summary>
        bool ConstrainRootMotion(const SceneLayout& before,SceneLayout& candidate,const std::vector<size_t>& actors,
            const std::filesystem::path& root,std::string& error) const;
        /// <summary>レイヤー・Trigger・除外IDを考慮し、最も近いColliderの交点を返します。</summary>
        std::optional<PhysicsRayHit> Raycast(const std::array<float,3>& origin,const std::array<float,3>& direction,float distance,
            unsigned int mask=0xffffffffu,bool includeTriggers=false,const std::string& ignore={}) const;
        /// <summary>直前に成功した更新のEnter・Stay・Exit通知を取得します。</summary>
        const std::vector<PhysicsContactEvent>& Events() const { return events_; }
        /// <summary>物理ワールド、接触履歴、予約したインパルスを破棄します。</summary>
        void Reset();
    private:
        struct Impl;
        std::shared_ptr<Impl> impl_;
        std::map<std::string,std::array<float,3>> impulses_;
        std::vector<PhysicsContactEvent> events_;
    };
}
