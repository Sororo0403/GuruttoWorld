#pragma once
#include <SceneRuntime/Scene/WorldState.h>
#include <SceneRuntime/Scene/SceneTransforms.h>
#include <unordered_map>

namespace SceneRuntime
{
    /// <summary>物理が使うコンポーネントの不変な世代だけを保持し、毎stepの深い複製を避けます。</summary>
    template <class T> class PhysicsValue final
    {
    public:
        /// <summary>未設定の物理値を作ります。</summary>
        PhysicsValue() = default;
        /// <summary>現在の実行値の世代を保持します。</summary>
        PhysicsValue(std::shared_ptr<const T> value) : value_(std::move(value)) {}
        /// <summary>旧呼出元の保存値を独立した世代へ変換します。</summary>
        PhysicsValue(const std::optional<T>& value) : value_(value ? std::make_shared<const T>(*value) : nullptr) {}
        /// <summary>値の有無を取得します。</summary>
        explicit operator bool() const noexcept
        {
            return static_cast<bool>(value_);
        }
        /// <summary>値の有無を取得します。</summary>
        bool HasValue() const noexcept
        {
            return static_cast<bool>(*this);
        }
        /// <summary>保持している不変な値を参照します。</summary>
        const T* operator->() const noexcept
        {
            return value_.get();
        }
        /// <summary>保持している不変な値を参照します。</summary>
        const T& operator*() const noexcept
        {
            return *value_;
        }
        /// <summary>世代が違う場合も、物理構築に使う値そのものを比較します。</summary>
        bool operator==(const PhysicsValue& other) const
        {
            return value_ == other.value_ || (value_ && other.value_ && *value_ == *other.value_);
        }

    private:
        std::shared_ptr<const T> value_;
    };
    struct PhysicsObject
    {
        EntityHandle entity;
        ComponentInstanceHandle colliderInstance, bodyInstance, controllerInstance, jointInstance;
        std::string id, parentId;
        std::array<float, 3> position{}, rotation{}, scale{1, 1, 1};
        PhysicsValue<BoxColliderComponent> boxCollider;
        PhysicsValue<RigidBodyComponent> rigidBody;
        PhysicsValue<PlayerControllerComponent> playerController;
        PhysicsValue<JointComponent> joint;
        PhysicsValue<TerrainComponent> terrain;
        PhysicsValue<TilemapComponent> tilemap;
        PhysicsValue<MeshRendererComponent> meshRenderer;
        struct Motion
        {
            bool enabled = false, rootMotion = false;
        };
        std::optional<Motion> animator;
        /// <summary>形状構築の参照先だけを取得します。</summary>
        std::filesystem::path Model() const
        {
            return meshRenderer ? meshRenderer->model : std::filesystem::path{};
        }
    };
    struct PhysicsFrame
    {
        struct Settings
        {
            std::array<float, 3> gravity{0, -9.81f, 0};
        } settings;
        std::vector<PhysicsObject> objects;
        /// <summary>実行表の物理値だけを保持し、保存型の配置・Script・UI・Prefabは複製しません。</summary>
        static PhysicsFrame Capture(WorldState::ReadView view, const std::map<std::string, bool>& rootMotions = {});
        /// <summary>移行中の保存型の呼出元を、物理専用表現へ変換します。</summary>
        static PhysicsFrame Capture(const SceneLayout& layout);
    };
    class PhysicsTransforms final
    {
    public:
        /// <summary>物理用の数値姿勢から行列を構築します。</summary>
        static bool Compose(const PhysicsObject& object, DirectX::XMFLOAT4X4& output);
        /// <summary>物理用の参照姿勢を保って数値だけを分解します。</summary>
        static bool ReadTransform(const DirectX::XMFLOAT4X4& matrix, const PhysicsObject& reference,
                                  PhysicsObject& output);
        /// <summary>物理表の親子行列を保存データなしで解決します。</summary>
        static bool Resolve(const PhysicsFrame& frame, std::vector<DirectX::XMFLOAT4X4>& output, std::string& error);
        /// <summary>ワールド行列を親基準の行列へ変換します。</summary>
        static bool WorldToLocal(const DirectX::XMFLOAT4X4& world, const DirectX::XMFLOAT4X4& parent,
                                 DirectX::XMFLOAT4X4& output)
        {
            return SceneTransforms::WorldToLocal(world, parent, output);
        }
        /// <summary>二つの数値行列を比較します。</summary>
        static bool Matches(const DirectX::XMFLOAT4X4& a, const DirectX::XMFLOAT4X4& b)
        {
            return SceneTransforms::Matches(a, b);
        }
    };
}
