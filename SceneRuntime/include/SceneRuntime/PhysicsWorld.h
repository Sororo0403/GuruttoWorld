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
        /// <summary>レイヤー・Trigger・除外IDを考慮し、最も近いColliderの交点を返します。</summary>
        std::optional<PhysicsRayHit> Raycast(const std::array<float,3>& origin,const std::array<float,3>& direction,float distance,
            unsigned int mask=0xffffffffu,bool includeTriggers=false,const std::string& ignore={}) const;
        /// <summary>直前に成功した更新のEnter・Stay・Exit通知を取得します。</summary>
        const std::vector<PhysicsContactEvent>& Events() const { return events_; }
        /// <summary>物理ワールド、接触履歴、予約したインパルスを破棄します。</summary>
        void Reset();
    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
        std::map<std::string,std::array<float,3>> impulses_;
        std::vector<PhysicsContactEvent> events_;
    };
}
