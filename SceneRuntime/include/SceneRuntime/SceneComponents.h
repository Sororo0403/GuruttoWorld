#pragma once
#include <SceneRuntime/ScriptValue.h>
#include <array>
#include <filesystem>
#include <string>
#include <map>
#include <vector>

namespace SceneRuntime
{
    // IDs are stable within their owning object. Transform is mandatory and has reserved ID "transform".
    struct MeshLod
    {
        float distance=20;
        std::filesystem::path model;
        bool operator==(const MeshLod&) const = default;
    };
    struct MeshRendererComponent
    {
        std::string id="mesh";
        bool enabled=true;
        std::filesystem::path model;
        std::string visibleWhen;
        std::vector<MeshLod> lods;
        bool instancing=false,occlusionCulling=false;
        bool operator==(const MeshRendererComponent&) const = default;
    };
    struct MaterialComponent
    {
        std::string id="material";
        bool enabled=true;
        std::filesystem::path asset;
        std::vector<std::filesystem::path> slots;
        bool operator==(const MaterialComponent&) const = default;
    };
    struct PlayerControllerComponent
    {
        std::string id="player";
        bool enabled=true;
        float moveSpeed=5;
        bool useGravity=false;
        float gravity=20;
        float jumpSpeed=7;
        bool usePhysics=false;
        float maxSlopeDegrees=45,stepHeight=0.3f;
        bool operator==(const PlayerControllerComponent&) const = default;
    };
    struct BoxColliderComponent
    {
        std::string id="collider";
        bool enabled=true;
        std::array<float,3> center{};
        std::array<float,3> size{1,1,1};
        std::string shape="box";
        float radius=0.5f,halfHeight=0.5f;
        std::filesystem::path model;
        bool isTrigger=false,convex=false;
        unsigned int layer=0,mask=0xffffffffu;
        bool operator==(const BoxColliderComponent&) const = default;
    };
    struct RigidBodyComponent
    {
        std::string id="rigidbody",motion="dynamic";
        bool enabled=true,continuous=true;
        bool planar=false;
        float mass=1,friction=0.5f,restitution=0,gravityScale=1,linearDamping=0.05f,angularDamping=0.05f;
        std::array<float,3> velocity{},angularVelocity{};
        bool operator==(const RigidBodyComponent&) const = default;
    };
    struct PrefabLink
    {
        std::filesystem::path asset;
        std::string sourceId,rootId,baseline;
        bool operator==(const PrefabLink&) const = default;
    };
    struct ScriptComponent
    {
        std::string id="script";
        bool enabled=true;
        std::string behaviour="Bob";
        std::map<std::string,float> parameters;
        ScriptValue::Object data;
        bool operator==(const ScriptComponent&) const = default;
        /// <summary>保存データ内のオブジェクト参照を複製先へ置き換えます。</summary>
        template<class IdMap> void Remap(const IdMap& ids)
        { for (auto& [name,value]:data) { static_cast<void>(name); value.Remap(ids); } }
    };
    struct RotatorComponent
    {
        std::string id="rotator";
        bool enabled=true;
        std::array<float,3> angularVelocity{0,90,0}; // Local Euler degrees per second.
        bool operator==(const RotatorComponent&) const = default;
    };
}
