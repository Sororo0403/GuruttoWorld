#pragma once
#include <SceneRuntime/ScriptValue.h>
#include <array>
#include <filesystem>
#include <string>
#include <map>
#include <vector>
#include <set>

namespace SceneRuntime
{
    // ID は所有元のオブジェクト内で安定して保持します。Transform は必須で、予約 ID "transform" を持ちます。
    struct MeshLod
    {
        float distance = 20;
        std::filesystem::path model;
        bool operator==(const MeshLod&) const = default;
    };
    struct MeshRendererComponent
    {
        std::string id = "mesh";
        bool enabled = true;
        std::filesystem::path model;
        std::string visibleWhen;
        std::vector<MeshLod> lods;
        bool instancing = false, occlusionCulling = false;
        bool operator==(const MeshRendererComponent&) const = default;
    };
    struct MaterialComponent
    {
        std::string id = "material";
        bool enabled = true;
        std::filesystem::path asset;
        std::vector<std::filesystem::path> slots;
        bool surfaceOverride = false;
        std::array<float, 4> color{1, 1, 1, 1};
        float roughness = .5f, metallic = 0;
        std::array<float, 4> shaderParameters{};
        bool operator==(const MaterialComponent&) const = default;
    };
    struct PlayerControllerComponent
    {
        std::string id = "player";
        bool enabled = true;
        float moveSpeed = 5;
        bool useGravity = false;
        float gravity = 20;
        float jumpSpeed = 7;
        bool usePhysics = false;
        float maxSlopeDegrees = 45, stepHeight = 0.3f;
        bool operator==(const PlayerControllerComponent&) const = default;
    };
    struct BoxColliderComponent
    {
        std::string id = "collider";
        bool enabled = true;
        std::array<float, 3> center{};
        std::array<float, 3> size{1, 1, 1};
        std::string shape = "box";
        float radius = 0.5f, halfHeight = 0.5f;
        std::filesystem::path model;
        bool isTrigger = false, convex = false;
        unsigned int layer = 0, mask = 0xffffffffu;
        bool operator==(const BoxColliderComponent&) const = default;
    };
    struct RigidBodyComponent
    {
        std::string id = "rigidbody", motion = "dynamic";
        bool enabled = true, continuous = true;
        bool planar = false;
        float mass = 1, friction = 0.5f, restitution = 0, gravityScale = 1, linearDamping = 0.05f,
              angularDamping = 0.05f;
        std::array<float, 3> velocity{}, angularVelocity{};
        bool operator==(const RigidBodyComponent&) const = default;
    };
    using PrefabBindings = std::map<std::string, ScriptObjectReference>;
    struct PrefabLink
    {
        std::filesystem::path asset;
        std::string sourceId, rootId, baseline;
        unsigned int referenceVersion = 1;
        PrefabBindings externalBindings{};
        bool operator==(const PrefabLink&) const = default;
    };
    struct ScriptComponent
    {
        std::string id = "script";
        bool enabled = true;
        std::string behaviour = "Bob";
        std::map<std::string, float> parameters;
        ScriptValue::Object data;
        std::set<std::string> sceneAssets{};
        bool operator==(const ScriptComponent&) const = default;
        /// <summary>保存データ内の型付き個体参照を走査します。</summary>
        template <class Visitor> void VisitObjectReferences(Visitor&& visitor)
        {
            for (auto& [name, value] : data)
            {
                static_cast<void>(name);
                value.VisitObjectReferences(visitor);
            }
        }
        /// <summary>保存データ内のオブジェクト参照を複製先へ置き換えます。</summary>
        template <class IdMap> void Remap(const IdMap& ids)
        {
            VisitObjectReferences(
                [&](std::string& id)
                {
                    if (const auto found = ids.find(id); found != ids.end())
                        id = found->second;
                });
        }
    };
    struct RotatorComponent
    {
        std::string id = "rotator";
        bool enabled = true;
        std::array<float, 3> angularVelocity{0, 90, 0}; // ローカル座標のオイラー角速度です。単位は度毎秒です。
        bool operator==(const RotatorComponent&) const = default;
    };
}
