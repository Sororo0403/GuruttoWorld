#pragma once
#include <SceneRuntime/Physics/PhysicsTypes.h>
#include <SceneRuntime/Physics/ScenePhysics.h>
#include <memory>
#include <Engine/Assets/AssetArtifact.h>
#include <SceneRuntime/Scene/WorldState.h>

namespace SceneRuntime
{
    struct PhysicsFrame;
    struct RuntimePose;
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
            Transaction(const Transaction&) = delete;
            Transaction& operator=(const Transaction&) = delete;
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
        PhysicsWorld(const PhysicsWorld&) = delete;
        PhysicsWorld& operator=(const PhysicsWorld&) = delete;
        /// <summary>入力と物理を更新し、成功した場合だけ配置・速度・接触を反映します。</summary>
        bool Advance(SceneLayout& layout, ScenePhysics::States& states, double seconds, float horizontal,
                     float vertical, bool jump, const std::filesystem::path& root, std::string& error);
        /// <summary>保存型を作らず型別の実行値を物理へ渡し、解決した姿勢だけを外側の候補へ反映します。</summary>
        bool Advance(WorldState::Transaction& transaction, ScenePhysics::States& states, double seconds,
                     float horizontal, float vertical, bool jump, const std::filesystem::path& root, std::string& error,
                     const std::map<std::string, bool>& rootMotions = {});
        /// <summary>次回の物理更新で適用するワールド空間のインパルスを予約します。</summary>
        bool AddImpulse(const std::string& object, const std::array<float, 3>& impulse);
        /// <summary>動的剛体の実行世代へ力積を予約し、同じ保存IDの再追加へ予約を引き継ぎません。</summary>
        bool AddImpulse(WorldState::ReadView view, EntityHandle entity, const std::array<float, 3>& impulse);
        /// <summary>段階末の実行値を問い合わせ用ワールドへ反映し、失われた世代の予約を破棄します。</summary>
        bool PrepareScene(WorldState::ReadView view, const ScenePhysics::States& states,
                          const std::filesystem::path& root, std::string& error, bool reloadAssets = false);
        /// <summary>ルート移動を独立した再利用可能な衝突ワールドで制限します。物理時計・速度・接触履歴は変更しません。</summary>
        bool ConstrainRootMotion(const SceneLayout& before, SceneLayout& candidate, const std::vector<size_t>& actors,
                                 const std::filesystem::path& root, std::string& error) const;
        /// <summary>RootMotion前の数値姿勢を使い、保存型を作らず実行候補の衝突を制限します。</summary>
        bool ConstrainRootMotion(WorldState::Transaction& transaction, const std::vector<RuntimePose>& previous,
                                 const std::filesystem::path& root, std::string& error) const;
        /// <summary>レイヤー・Trigger・除外IDを考慮し、最も近いColliderの交点を返します。</summary>
        std::optional<PhysicsRayHit> Raycast(const std::array<float, 3>& origin, const std::array<float, 3>& direction,
                                             float distance, unsigned int mask = 0xffffffffu,
                                             bool includeTriggers = false, const std::string& ignore = {}) const;
        /// <summary>直前に成功した更新のEnter・Stay・Exit通知を取得します。</summary>
        const std::vector<PhysicsContactEvent>& Events() const
        {
            return events_;
        }
        /// <summary>物理ワールド、接触履歴、予約したインパルスを破棄します。</summary>
        void Reset();

    private:
        friend class RuntimePhysics;
        /// <summary>外側の保存点を使って固定更新を進め、同じ物理状態の重複保存を避けます。</summary>
        bool AdvanceCandidate(WorldState::Transaction& transaction, ScenePhysics::States& states, double seconds,
                              float horizontal, float vertical, bool jump, const std::filesystem::path& root,
                              std::string& error, const std::map<std::string, bool>& rootMotions,
                              const Transaction& savepoint);
        /// <summary>物理専用の不変入力と数値姿勢を使用してstepを進めます。</summary>
        bool AdvanceFrame(PhysicsFrame& frame, ScenePhysics::States& states, double seconds, float horizontal,
                          float vertical, bool jump, const std::filesystem::path& root, std::string& error,
                          const SceneLayout* legacyValidation = nullptr, bool hasSavepoint = false);
        /// <summary>物理専用の数値姿勢でルート移動の衝突を解決します。</summary>
        bool ConstrainFrame(const PhysicsFrame& before, PhysicsFrame& candidate, const std::vector<size_t>& actors,
                            const std::filesystem::path& root, std::string& error) const;
        struct Impl;
        std::shared_ptr<Impl> impl_;
        mutable std::shared_ptr<Impl> rootQueries_;
        mutable std::filesystem::path rootQueryRoot_;
        mutable std::vector<Engine::ArtifactSources> rootQueryFiles_;
        std::map<std::string, std::array<float, 3>> impulses_;
        std::map<std::string, ComponentInstanceHandle> impulseOwners_;
        std::vector<PhysicsContactEvent> events_;
    };
}
