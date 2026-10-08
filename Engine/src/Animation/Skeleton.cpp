#include <Engine/Animation/Skeleton.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
    template<size_t Size>
    std::array<float,Size> Value(const std::vector<Engine::BoneKey<Size>>& keys,float time,const std::array<float,Size>& fallback)
    {
        if (keys.empty()) return fallback;
        if (time<=keys.front().time) return keys.front().value;
        if (time>=keys.back().time) return keys.back().value;
        const auto next=std::upper_bound(keys.begin(),keys.end(),time,[](float value,const auto& key) { return value<key.time; });
        const auto& previous=*(next-1); const float amount=(time-previous.time)/(next->time-previous.time);
        std::array<float,Size> result;
        if constexpr(Size==4)
        {
            DirectX::XMFLOAT4 rotation;
            DirectX::XMStoreFloat4(&rotation,DirectX::XMQuaternionNormalize(DirectX::XMQuaternionSlerp(
                DirectX::XMVectorSet(previous.value[0],previous.value[1],previous.value[2],previous.value[3]),
                DirectX::XMVectorSet(next->value[0],next->value[1],next->value[2],next->value[3]),amount)));
            result={rotation.x,rotation.y,rotation.z,rotation.w};
        }
        else for (size_t index=0;index<Size;++index) result[index]=previous.value[index]+(next->value[index]-previous.value[index])*amount;
        return result;
    }
}
namespace Engine
{
    std::vector<BonePose> Skeleton::Sample(const SkeletonData& rig,const std::string& name,double seconds,bool loop)
    {
        if (!std::isfinite(seconds) || seconds<0) throw std::runtime_error("Invalid skeletal animation time");
        std::vector<BonePose> pose;
        for (const auto& node : rig.nodes) pose.push_back(node.rest);
        if (name.empty()) return pose;
        const auto clip=std::find_if(rig.clips.begin(),rig.clips.end(),[&](const auto& value) { return value.name==name; });
        if (clip==rig.clips.end()) throw std::runtime_error("Unknown skeletal clip: "+name);
        if (!std::isfinite(clip->duration) || clip->duration<=0) throw std::runtime_error("Invalid skeletal clip duration");
        const float time=static_cast<float>(loop ? std::fmod(seconds,clip->duration) : std::min(seconds,static_cast<double>(clip->duration)));
        for (const auto& [node,track] : clip->tracks)
        {
            auto& value=pose.at(node);
            value.position=Value(track.positions,time,value.position); value.rotation=Value(track.rotations,time,value.rotation); value.scale=Value(track.scales,time,value.scale);
        }
        return pose;
    }
    std::vector<BonePose> Skeleton::Blend(const std::vector<BonePose>& first,const std::vector<BonePose>& second,float amount)
    {
        if (first.size()!=second.size() || !std::isfinite(amount)) throw std::runtime_error("Invalid skeletal blend");
        amount=std::clamp(amount,0.0f,1.0f); auto result=first;
        for (size_t node=0;node<result.size();++node)
        {
            for (size_t axis=0;axis<3;++axis)
            {
                result[node].position[axis]+=(second[node].position[axis]-first[node].position[axis])*amount;
                result[node].scale[axis]+=(second[node].scale[axis]-first[node].scale[axis])*amount;
            }
            DirectX::XMFLOAT4 rotation;
            DirectX::XMStoreFloat4(&rotation,DirectX::XMQuaternionNormalize(DirectX::XMQuaternionSlerp(
                DirectX::XMVectorSet(first[node].rotation[0],first[node].rotation[1],first[node].rotation[2],first[node].rotation[3]),
                DirectX::XMVectorSet(second[node].rotation[0],second[node].rotation[1],second[node].rotation[2],second[node].rotation[3]),amount)));
            result[node].rotation={rotation.x,rotation.y,rotation.z,rotation.w};
        }
        return result;
    }
    std::vector<DirectX::XMFLOAT4X4> Skeleton::Matrices(const SkeletonData& rig,const std::vector<BonePose>& pose)
    {
        if (pose.size()!=rig.nodes.size()) throw std::runtime_error("Invalid skeleton pose size");
        std::vector<DirectX::XMFLOAT4X4> matrices(pose.size());
        for (size_t node=0;node<pose.size();++node)
        {
            const auto& value=pose[node]; const auto parent=rig.nodes[node].parent;
            if (parent>=static_cast<int>(node) || parent<-1) throw std::runtime_error("Invalid skeleton hierarchy");
            auto matrix=DirectX::XMMatrixScaling(value.scale[0],value.scale[1],value.scale[2])*
                DirectX::XMMatrixRotationQuaternion(DirectX::XMQuaternionNormalize(DirectX::XMVectorSet(value.rotation[0],value.rotation[1],value.rotation[2],value.rotation[3])))*
                DirectX::XMMatrixTranslation(value.position[0],value.position[1],value.position[2]);
            if (parent>=0) matrix=matrix*DirectX::XMLoadFloat4x4(&matrices[static_cast<size_t>(parent)]);
            DirectX::XMStoreFloat4x4(&matrices[node],matrix);
            for (const auto& row : matrices[node].m) for (const float entry : row) if (!std::isfinite(entry)) throw std::runtime_error("Nonfinite skeleton matrix");
        }
        return matrices;
    }
    MeshData Skeleton::BindMesh(const RiggedMesh& mesh)
    {
        if (mesh.weights.size()!=mesh.mesh.vertices.size() || mesh.joints.size()>256) throw std::runtime_error("Invalid GPU skin weights");
        auto result=mesh.mesh;
        for (size_t vertex=0;vertex<result.vertices.size();++vertex)
        {
            const auto& source=mesh.weights[vertex]; auto& target=result.vertices[vertex]; float total=0;
            for (size_t influence=0;influence<4;++influence)
            {
                const float weight=source.weights[influence];
                if (!std::isfinite(weight) || weight<0 || (weight>0 && source.joints[influence]>=mesh.joints.size())) throw std::runtime_error("Invalid GPU bone influence");
                total+=weight;
            }
            if (!std::isfinite(total)) throw std::runtime_error("Invalid GPU total weight");
            target.joints=source.joints;
            for (size_t influence=0;influence<4;++influence) target.weights[influence]=total>0 ? source.weights[influence]/total : 0;
        }
        return result;
    }
    std::vector<SkinMatrix> Skeleton::Palette(const SkeletonData& rig,const RiggedMesh& mesh,const std::vector<DirectX::XMFLOAT4X4>& matrices)
    {
        if (mesh.joints.size()!=mesh.inverseBind.size() || mesh.joints.size()>256 || !std::isfinite(rig.importScale) || rig.importScale<=0)
            throw std::runtime_error("Invalid GPU skin palette");
        std::vector<SkinMatrix> palette(mesh.joints.size()+1);
        const auto root=DirectX::XMLoadFloat4x4(&rig.inverseRoot);
        const auto scale=DirectX::XMMatrixScaling(rig.importScale,rig.importScale,rig.importScale);
        for (size_t index=0;index<palette.size();++index)
        {
            auto matrix=index==0 ? DirectX::XMLoadFloat4x4(&matrices.at(mesh.node))*root :
                DirectX::XMLoadFloat4x4(&mesh.inverseBind[index-1])*DirectX::XMLoadFloat4x4(&matrices.at(mesh.joints[index-1]))*root;
            const auto determinant=DirectX::XMVectorGetX(DirectX::XMMatrixDeterminant(matrix));
            if (!std::isfinite(determinant) || determinant==0) throw std::runtime_error("Singular skin transform");
            DirectX::XMStoreFloat4x4(&palette[index].position,matrix*scale);
            DirectX::XMStoreFloat4x4(&palette[index].normal,DirectX::XMMatrixTranspose(DirectX::XMMatrixInverse(nullptr,matrix)));
            for (const auto* value : {&palette[index].position,&palette[index].normal})
                for (const auto& row : value->m) for (const float entry : row)
                    if (!std::isfinite(entry)) throw std::runtime_error("Nonfinite GPU skin matrix");
        }
        return palette;
    }
    MeshData Skeleton::Skin(const SkeletonData& rig,const RiggedMesh& mesh,const std::vector<DirectX::XMFLOAT4X4>& matrices)
    {
        if (mesh.weights.size()!=mesh.mesh.vertices.size() || mesh.joints.size()!=mesh.inverseBind.size()) throw std::runtime_error("Invalid skin weights");
        std::vector<DirectX::XMFLOAT4X4> palette(mesh.joints.size()),normalPalette(mesh.joints.size());
        for (size_t joint=0;joint<palette.size();++joint)
        {
            const auto matrix=DirectX::XMLoadFloat4x4(&mesh.inverseBind[joint])*DirectX::XMLoadFloat4x4(&matrices.at(mesh.joints[joint]))*DirectX::XMLoadFloat4x4(&rig.inverseRoot);
            DirectX::XMStoreFloat4x4(&palette[joint],matrix);
            DirectX::XMStoreFloat4x4(&normalPalette[joint],DirectX::XMMatrixTranspose(DirectX::XMMatrixInverse(nullptr,matrix)));
        }
        const auto rigid=DirectX::XMLoadFloat4x4(&matrices.at(mesh.node))*DirectX::XMLoadFloat4x4(&rig.inverseRoot);
        const auto rigidNormal=DirectX::XMMatrixTranspose(DirectX::XMMatrixInverse(nullptr,rigid));
        auto result=mesh.mesh;
        for (size_t vertex=0;vertex<result.vertices.size();++vertex)
        {
            const auto& source=mesh.mesh.vertices[vertex]; const auto& skin=mesh.weights[vertex];
            const auto p=DirectX::XMVectorSet(source.position[0],source.position[1],source.position[2],1);
            const auto n=DirectX::XMVectorSet(source.normal[0],source.normal[1],source.normal[2],0);
            auto position=DirectX::XMVectorZero(),normal=DirectX::XMVectorZero(); float total=0;
            for (size_t influence=0;influence<4;++influence)
            {
                const float weight=skin.weights[influence];
                if (!std::isfinite(weight) || weight<0) throw std::runtime_error("Invalid bone weight");
                if (weight==0) continue;
                position=DirectX::XMVectorAdd(position,DirectX::XMVectorScale(DirectX::XMVector3TransformCoord(p,DirectX::XMLoadFloat4x4(&palette.at(skin.joints[influence]))),weight));
                normal=DirectX::XMVectorAdd(normal,DirectX::XMVectorScale(DirectX::XMVector3TransformNormal(n,DirectX::XMLoadFloat4x4(&normalPalette.at(skin.joints[influence]))),weight)); total+=weight;
            }
            if (total==0) { position=DirectX::XMVector3TransformCoord(p,rigid); normal=DirectX::XMVector3TransformNormal(n,rigidNormal); }
            else position=DirectX::XMVectorScale(position,1/total);
            DirectX::XMFLOAT3 outPosition,outNormal;
            DirectX::XMStoreFloat3(&outPosition,position); DirectX::XMStoreFloat3(&outNormal,DirectX::XMVector3Normalize(normal));
            result.vertices[vertex].position={outPosition.x*rig.importScale,outPosition.y*rig.importScale,outPosition.z*rig.importScale}; result.vertices[vertex].normal={outNormal.x,outNormal.y,outNormal.z};
            for (float entry : result.vertices[vertex].position) if (!std::isfinite(entry)) throw std::runtime_error("Nonfinite skinned position");
            for (float entry : result.vertices[vertex].normal) if (!std::isfinite(entry)) throw std::runtime_error("Nonfinite skinned normal");
        }
        return result;
    }
}
