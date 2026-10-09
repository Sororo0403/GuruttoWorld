#pragma once
#include <Engine/Animation/Skeleton.h>
#include <map>
#include <set>
namespace SceneRuntime
{
    struct SceneLayout;
    struct ScenePlacement;
    struct RagdollBone
    {
        std::string bone,body;
        std::array<float,16> bindOffset{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        bool operator==(const RagdollBone&) const = default;
    };
    struct RagdollComponent
    {
        std::string id="ragdoll";
        bool enabled=true,active=false;
        float weight=1,bodyMass=1,radius=.08f,swing=45,twist=45;
        std::vector<RagdollBone> bones;
        bool operator==(const RagdollComponent&) const = default;
        /// <summary>骨の対応と生成用物理設定の保存範囲を検証します。</summary>
        void Validate() const;
        /// <summary>PrefabとHierarchy複製で骨の剛体参照を新しいIDへ置換します。</summary>
        template<class IdMap> void Remap(const IdMap& ids)
        { for(auto& binding:bones) if(const auto found=ids.find(binding.body);found!=ids.end()) binding.body=found->second; }
    };
    class Ragdoll final
    {
    public:
        /// <summary>Animator骨格の対応と生成済みCollider・剛体を検証します。</summary>
        static void Validate(const SceneLayout&,const ScenePlacement&,const Engine::SkeletonData&);
        /// <summary>現在の姿勢からCollider・剛体・Jointを一括生成します。失敗時は配置を保持します。</summary>
        static bool Generate(SceneLayout&,const std::string& owner,const Engine::SkeletonData&,const std::vector<Engine::BonePose>&,std::string& error);
        /// <summary>非activeの剛体を骨の姿勢へ追従させ、activeの剛体は物理制御へ切り替えます。</summary>
        static void Synchronize(SceneLayout&,const std::string& owner,const Engine::SkeletonData&,const std::vector<Engine::BonePose>&);
        /// <summary>物理剛体のワールド姿勢を骨のローカル姿勢へ変換し、指定重みで合成します。</summary>
        static std::vector<Engine::BonePose> Pose(const SceneLayout&,const ScenePlacement&,const Engine::SkeletonData&,const std::vector<Engine::BonePose>&);
    };
}
