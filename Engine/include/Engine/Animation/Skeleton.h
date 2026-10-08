#pragma once
#include <Engine/Graphics/Models/MeshData.h>
#include <Engine/Animation/SkinMatrix.h>
#include <DirectXMath.h>
#include <map>
#include <memory>
#include <string>

namespace Engine
{
    struct BonePose
    {
        std::array<float,3> position{},scale{1,1,1};
        std::array<float,4> rotation{0,0,0,1};
    };
    struct SkeletonNode { std::string name; int parent=-1; BonePose rest; };
    template<size_t Size> struct BoneKey { float time=0; std::array<float,Size> value{}; };
    struct BoneTrack { std::vector<BoneKey<3>> positions,scales; std::vector<BoneKey<4>> rotations; };
    struct SkeletalClip { std::string name; float duration=0; std::map<size_t,BoneTrack> tracks; };
    struct SkinWeights { std::array<unsigned int,4> joints{}; std::array<float,4> weights{}; };
    struct RiggedMesh
    {
        MeshData mesh;
        size_t node=0;
        std::vector<size_t> joints;
        std::vector<DirectX::XMFLOAT4X4> inverseBind;
        std::vector<SkinWeights> weights;
    };
    struct SkeletonData
    {
        std::vector<SkeletonNode> nodes;
        std::vector<RiggedMesh> meshes;
        std::vector<SkeletalClip> clips;
        DirectX::XMFLOAT4X4 inverseRoot{};
        float importScale=1;
    };
    class Skeleton final
    {
    public:
        static std::vector<BonePose> Sample(const SkeletonData& rig,const std::string& clip,double seconds,bool loop);
        static std::vector<BonePose> Blend(const std::vector<BonePose>& first,const std::vector<BonePose>& second,float amount);
        static std::vector<DirectX::XMFLOAT4X4> Matrices(const SkeletonData& rig,const std::vector<BonePose>& pose);
        static MeshData Skin(const SkeletonData& rig,const RiggedMesh& mesh,const std::vector<DirectX::XMFLOAT4X4>& matrices);
        /// <summary>静的な頂点へ正規化したボーン番号とウェイトを設定します。</summary>
        static MeshData BindMesh(const RiggedMesh& mesh);
        /// <summary>GPUとCPUで共有する位置・逆転置法線行列を計算します。先頭は剛体ノード用です。</summary>
        static std::vector<SkinMatrix> Palette(const SkeletonData& rig,const RiggedMesh& mesh,const std::vector<DirectX::XMFLOAT4X4>& matrices);
        static std::shared_ptr<const SkeletonData> Load(const std::filesystem::path& path,std::string& error);
    };
}
