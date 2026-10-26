#pragma once
#include <SceneRuntime/Scene/WorldDefinition.h>
#include <SceneRuntime/Scene/SceneEdits.h>
#include <SceneRuntime/Scene/EntityHandle.h>
#include <span>

namespace SceneRuntime
{
#define ENGINE_RUNTIME_OPTIONAL(Type, member, references, assets, order) std::optional<Type>,
#define ENGINE_RUNTIME_REPEATED(Type, member, references, assets, order) std::vector<Type>,
    using RuntimeComponentValue =
        std::variant<ENGINE_SCENE_COMPONENTS(ENGINE_RUNTIME_OPTIONAL, ENGINE_RUNTIME_REPEATED) std::monostate>;
#undef ENGINE_RUNTIME_OPTIONAL
#undef ENGINE_RUNTIME_REPEATED

    /// <summary>不変の定義と型別の実行値を分け、未確定の書き込みは取引の候補にだけ保持します。</summary>
    class WorldState final
    {
        struct Impl;
        struct Pending;

    public:
        class Transaction;
        class ReadView final
        {
        public:
            /// <summary>保存IDを、このWorldの生存する実行ハンドルへ解決します。</summary>
            std::optional<EntityHandle> Resolve(std::string_view id) const;
            /// <summary>別World・破棄済み・再利用前のハンドルを拒否します。</summary>
            bool Alive(EntityHandle entity) const noexcept;
            /// <summary>コンポーネントの削除・同じ保存IDでの再追加を区別して生存を確認します。</summary>
            bool Alive(ComponentInstanceHandle component) const noexcept;
            /// <summary>次の確定まで有効な、定義順の生存ハンドルを借用します。</summary>
            std::span<const EntityHandle> Entities() const;
            /// <summary>ローカル姿勢を値で取得します。古いハンドルは未設定です。</summary>
            std::optional<LocalTransform> Transform(EntityHandle entity) const;
            /// <summary>現在の親ハンドルを返します。ルートまたは無効な対象は未設定です。</summary>
            std::optional<EntityHandle> Parent(EntityHandle entity) const;
            /// <summary>親子を反映したワールド行列を値で取得します。</summary>
            std::optional<DirectX::XMFLOAT4X4> WorldMatrix(EntityHandle entity) const;
            /// <summary>保存IDを値で取得します。実行用ハンドル自体は保存しません。</summary>
            std::optional<std::string> ObjectId(EntityHandle entity) const;
            /// <summary>表示名から現在の生存ハンドルを取得します。</summary>
            std::vector<EntityHandle> FindByName(std::string_view name) const;
            /// <summary>型付きコンポーネントを独立した値として取得します。</summary>
            template <class T> std::optional<T> Component(EntityHandle entity) const
            {
                auto value = Value(entity, std::optional<T>{});
                return std::get<std::optional<T>>(std::move(value));
            }
            /// <summary>同じ型を複数保持するコンポーネントの順序付きの値を取得します。</summary>
            template <class T> std::vector<T> Repeated(EntityHandle entity) const
            {
                auto value = Value(entity, std::vector<T>{});
                return std::get<std::vector<T>>(std::move(value));
            }
            /// <summary>現在の値の不変な世代を保持します。次の確定後も同じ値を安全に参照できます。</summary>
            template <class T> std::shared_ptr<const T> ComponentLease(EntityHandle entity) const
            {
                return std::static_pointer_cast<const T>(LeaseValue(entity, std::optional<T>{}));
            }
            /// <summary>複数コンポーネントの現在の値の、不変な世代を保持します。</summary>
            template <class T> std::shared_ptr<const std::vector<T>> RepeatedLease(EntityHandle entity) const
            {
                return std::static_pointer_cast<const std::vector<T>>(LeaseValue(entity, std::vector<T>{}));
            }
            /// <summary>単一コンポーネントの実行用識別子を取得します。</summary>
            template <class T> std::optional<ComponentInstanceHandle> Instance(EntityHandle entity) const
            {
                return InstanceValue(entity, std::optional<T>{}, {});
            }
            /// <summary>複数コンポーネントの保存IDから、現在の実行用識別子を取得します。</summary>
            template <class T>
            std::optional<ComponentInstanceHandle> RepeatedInstance(EntityHandle entity, std::string_view id) const
            {
                return InstanceValue(entity, std::vector<T>{}, id);
            }
            /// <summary>指定した型を持つ個体を、次の確定まで借用します。</summary>
            template <class T> std::span<const EntityHandle> With() const
            {
                return WithValue(std::optional<T>{});
            }
            /// <summary>同じ型を複数保持するコンポーネントの所有個体を、次の確定まで借用します。</summary>
            template <class T> std::span<const EntityHandle> WithRepeated() const
            {
                return WithValue(std::vector<T>{});
            }
            /// <summary>構築元の不変定義を取得します。実行値の変更でこの定義は書き換わりません。</summary>
            std::shared_ptr<const WorldDefinition> Definition() const;
            /// <summary>旧SDKや構造変更向けに一つの個体を値へ変換します。通常の型別走査では使いません。</summary>
            std::optional<ScenePlacement> Object(EntityHandle entity) const;
            /// <summary>構造変更・明示的な互換操作のため保存型へ変換します。通常のtickでは使いません。</summary>
            SceneLayout ExportLayout() const;

        private:
            friend class WorldState;
            friend class Transaction;
            /// <summary>確定値、または指定した取引の候補を借用します。</summary>
            ReadView(const WorldState& owner, std::uint64_t transaction) noexcept;
            /// <summary>型を指定して、コンポーネントの値を取得します。</summary>
            RuntimeComponentValue Value(EntityHandle entity, RuntimeComponentValue empty) const;
            /// <summary>型付きストレージの世代を保持し、変更候補は独立した不変値へ変換します。</summary>
            std::shared_ptr<const void> LeaseValue(EntityHandle entity, RuntimeComponentValue empty) const;
            /// <summary>型と保存IDに対応するコンポーネント世代を取得します。</summary>
            std::optional<ComponentInstanceHandle> InstanceValue(EntityHandle entity, RuntimeComponentValue empty,
                                                                 std::string_view id) const;
            /// <summary>型を指定して、生存する所有個体を取得します。</summary>
            std::span<const EntityHandle> WithValue(const RuntimeComponentValue& empty) const;
            const WorldState* owner_;
            std::uint64_t transaction_;
        };

        class Transaction final
        {
        public:
            /// <summary>確定していない候補を破棄し、元のWorldを保持します。</summary>
            ~Transaction();
            /// <summary>取引の所有権は複製できません。</summary>
            Transaction(const Transaction&) = delete;
            /// <summary>取引の所有権は代入できません。</summary>
            Transaction& operator=(const Transaction&) = delete;
            /// <summary>この取引で準備している値だけを読むビューを取得します。</summary>
            ReadView Read() const noexcept;
            /// <summary>姿勢の候補と変更を記録し、確定済みの値は保持します。</summary>
            SceneEditResult SetLocalTransform(EntityHandle entity, const LocalTransform& value, std::string& error);
            /// <summary>型付きコンポーネントを候補へ設定します。追加・削除は構造変更として扱います。</summary>
            template <class T>
            SceneEditResult SetComponent(EntityHandle entity, std::optional<T> value, std::string& error)
            {
                return SetValue(entity, RuntimeComponentValue{std::move(value)}, error);
            }
            /// <summary>複数コンポーネントの安定IDと順序を保った候補を設定します。</summary>
            template <class T>
            SceneEditResult SetRepeated(EntityHandle entity, std::vector<T> value, std::string& error)
            {
                return SetValue(entity, RuntimeComponentValue{std::move(value)}, error);
            }
            /// <summary>構造変更の時だけ保存型の候補を作り、新しい定義と実行値をまとめて準備します。</summary>
            SceneEditResult ApplyStructure(const SceneEdit& edit, std::string& error);
            /// <summary>親子行列と公開通知を準備します。失敗時は未確定候補を破棄できます。</summary>
            bool Prepare(std::string& error);
            /// <summary>準備済み候補の差分だけを取得します。以前の未取得通知は含みません。</summary>
            const SceneChangeSet& Changes() const noexcept;
            /// <summary>準備済み値を確保や利用者コールバックなしで反映します。未準備の取引は拒否します。</summary>
            bool Commit() noexcept;

        private:
            friend class WorldState;
            /// <summary>一つのwriterだけが所有する候補を作ります。</summary>
            explicit Transaction(WorldState& owner);
            /// <summary>型付きの値を内部の変更表へ追加します。</summary>
            SceneEditResult SetValue(EntityHandle entity, RuntimeComponentValue value, std::string& error);
            WorldState* owner_;
            std::unique_ptr<Pending> pending_;
        };

        /// <summary>不変の定義から、独立した実行状態とWorld識別子を作ります。</summary>
        explicit WorldState(std::shared_ptr<const WorldDefinition> definition);
        /// <summary>実行状態と型別の格納領域を解放します。</summary>
        ~WorldState();
        /// <summary>World識別子を暗黙に複製しないよう、コピーを禁止します。</summary>
        WorldState(const WorldState&) = delete;
        /// <summary>所有中のハンドルを別Worldへ移さないよう、代入を禁止します。</summary>
        WorldState& operator=(const WorldState&) = delete;
        /// <summary>確定済みの値だけを読むビューを取得します。</summary>
        ReadView Read() const noexcept;
        /// <summary>全体の保存データを複製せず、空の書き込み候補を作ります。</summary>
        Transaction BeginTransaction();
        /// <summary>成功した取引の未取得通知を一度だけ取得します。準備中は既存通知を保持します。</summary>
        SceneChangeSet TakeChanges() noexcept;

    private:
        std::unique_ptr<Impl> impl_;
    };
}
