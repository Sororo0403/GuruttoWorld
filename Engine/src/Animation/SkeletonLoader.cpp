#include <Engine/Animation/Skeleton.h>
#include <Engine/Assets/AssetDatabase.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/material.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
    using namespace Engine;
    DirectX::XMFLOAT4X4 Matrix(const aiMatrix4x4& value)
    {
        return {value.a1,value.b1,value.c1,value.d1,value.a2,value.b2,value.c2,value.d2,
            value.a3,value.b3,value.c3,value.d3,value.a4,value.b4,value.c4,value.d4};
    }
    void Nodes(const aiNode& source,int parent,SkeletonData& rig,std::map<std::string,size_t>& names,std::vector<size_t>& owners)
    {
        if (rig.nodes.size()>=512) throw std::runtime_error("Skeleton exceeds 512 nodes");
        const auto index=rig.nodes.size();
        SkeletonNode node; node.name=source.mName.C_Str(); node.parent=parent;
        aiVector3D scale,position; aiQuaternion rotation; source.mTransformation.Decompose(scale,rotation,position);
        node.rest.position={position.x,position.y,position.z}; node.rest.scale={scale.x,scale.y,scale.z}; node.rest.rotation={rotation.x,rotation.y,rotation.z,rotation.w};
        if (!names.emplace(node.name,index).second) throw std::runtime_error("Duplicate skeleton node names");
        rig.nodes.push_back(std::move(node));
        for (unsigned int mesh=0;mesh<source.mNumMeshes;++mesh)
        {
            auto& owner=owners.at(source.mMeshes[mesh]);
            if (owner!=static_cast<size_t>(-1)) throw std::runtime_error("Instanced skeletal meshes are unsupported");
            owner=index;
        }
        for (unsigned int child=0;child<source.mNumChildren;++child) Nodes(*source.mChildren[child],static_cast<int>(index),rig,names,owners);
    }
    MeshData Geometry(const aiScene& scene,const aiMesh& source,const std::filesystem::path& path)
    {
        if (!source.HasPositions() || !source.HasNormals()) throw std::runtime_error("Skeletal mesh lacks positions or normals");
        MeshData mesh; aiColor3D diffuse(1,1,1);
        if (source.mMaterialIndex<scene.mNumMaterials)
        {
            const auto& material=*scene.mMaterials[source.mMaterialIndex]; material.Get(AI_MATKEY_COLOR_DIFFUSE,diffuse);
            aiString texture;
            if (material.GetTexture(aiTextureType_DIFFUSE,0,&texture)==AI_SUCCESS && texture.length)
            {
                if (texture.C_Str()[0]=='*') throw std::runtime_error("Embedded skeletal textures are unsupported; export external textures");
                mesh.texturePath=(path.parent_path()/std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(texture.C_Str())))).lexically_normal();
            }
        }
        for (unsigned int vertex=0;vertex<source.mNumVertices;++vertex)
        {
            const auto& p=source.mVertices[vertex]; const auto& n=source.mNormals[vertex];
            const auto uv=source.HasTextureCoords(0) ? source.mTextureCoords[0][vertex] : aiVector3D{};
            const auto color=source.HasVertexColors(0) ? source.mColors[0][vertex] : aiColor4D(1,1,1,1);
            mesh.vertices.push_back({{p.x,p.y,p.z},{n.x,n.y,n.z},{uv.x,uv.y},{color.r*diffuse.r,color.g*diffuse.g,color.b*diffuse.b,color.a}});
        }
        for (unsigned int face=0;face<source.mNumFaces;++face)
        {
            const auto& indices=source.mFaces[face]; if (indices.mNumIndices!=3) throw std::runtime_error("Nontriangle skeletal face");
            mesh.indices.insert(mesh.indices.end(),indices.mIndices,indices.mIndices+3);
        }
        return mesh;
    }
    float Time(double ticks,double frequency)
    {
        const auto value=ticks/frequency;
        if (!std::isfinite(value) || value<0 || value>1000000) throw std::runtime_error("Invalid skeletal key time");
        return static_cast<float>(value);
    }
}
namespace Engine
{
    std::shared_ptr<const SkeletonData> Skeleton::Load(const std::filesystem::path& path,std::string& error)
    {
        try
        {
            Assimp::Importer importer; const auto utf8=path.u8string();
            const auto* scene=importer.ReadFile(reinterpret_cast<const char*>(utf8.c_str()),aiProcess_Triangulate|aiProcess_GenNormals|
                aiProcess_ConvertToLeftHanded|aiProcess_SortByPType|aiProcess_ValidateDataStructure);
            if (!scene || !scene->mRootNode || (scene->mFlags&AI_SCENE_FLAGS_INCOMPLETE)) throw std::runtime_error(importer.GetErrorString());
            auto rig=std::make_shared<SkeletonData>(); std::map<std::string,size_t> names;
            std::vector<size_t> owners(scene->mNumMeshes,static_cast<size_t>(-1));
            Nodes(*scene->mRootNode,-1,*rig,names,owners);
            const auto root=Matrix(scene->mRootNode->mTransformation);
            DirectX::XMStoreFloat4x4(&rig->inverseRoot,DirectX::XMMatrixInverse(nullptr,DirectX::XMLoadFloat4x4(&root)));
            for (unsigned int meshIndex=0;meshIndex<scene->mNumMeshes;++meshIndex)
            {
                const auto& source=*scene->mMeshes[meshIndex];
                if (!(source.mPrimitiveTypes&aiPrimitiveType_TRIANGLE)) continue;
                RiggedMesh mesh; mesh.node=owners[meshIndex]; mesh.mesh=Geometry(*scene,source,path); mesh.weights.resize(source.mNumVertices);
                if (source.mNumBones>256) throw std::runtime_error("Mesh exceeds 256 bones");
                std::vector<std::vector<std::pair<unsigned int,float>>> influences(source.mNumVertices);
                for (unsigned int bone=0;bone<source.mNumBones;++bone)
                {
                    const auto& joint=*source.mBones[bone]; const auto found=names.find(joint.mName.C_Str());
                    if (found==names.end()) throw std::runtime_error("Bone node missing");
                    mesh.joints.push_back(found->second); mesh.inverseBind.push_back(Matrix(joint.mOffsetMatrix));
                    for (unsigned int weight=0;weight<joint.mNumWeights;++weight)
                    {
                        const auto& value=joint.mWeights[weight];
                        if (!std::isfinite(value.mWeight) || value.mWeight<0) throw std::runtime_error("Invalid imported bone weight");
                        influences.at(value.mVertexId).emplace_back(bone,value.mWeight);
                    }
                }
                for (size_t vertex=0;vertex<mesh.weights.size();++vertex)
                {
                    auto& values=influences[vertex]; std::stable_sort(values.begin(),values.end(),[](const auto& a,const auto& b) { return a.second>b.second; });
                    for (size_t weight=0;weight<std::min(size_t(4),values.size());++weight)
                    { mesh.weights[vertex].joints[weight]=values[weight].first; mesh.weights[vertex].weights[weight]=values[weight].second; }
                }
                rig->meshes.push_back(std::move(mesh));
            }
            if (rig->meshes.empty()) throw std::runtime_error("No skeletal triangles");
            for (unsigned int index=0;index<scene->mNumAnimations;++index)
            {
                const auto& source=*scene->mAnimations[index]; const double frequency=source.mTicksPerSecond>0 ? source.mTicksPerSecond : 25;
                SkeletalClip clip; clip.name=source.mName.length ? source.mName.C_Str() : "Clip"+std::to_string(index);
                if (std::any_of(rig->clips.begin(),rig->clips.end(),[&](const auto& value) { return value.name==clip.name; })) clip.name+="-"+std::to_string(index);
                clip.duration=std::max(0.0001f,Time(source.mDuration,frequency));
                for (unsigned int channel=0;channel<source.mNumChannels;++channel)
                {
                    const auto& sourceTrack=*source.mChannels[channel]; const auto node=names.find(sourceTrack.mNodeName.C_Str());
                    if (node==names.end()) throw std::runtime_error("Animation node missing");
                    BoneTrack track;
                    for (unsigned int key=0;key<sourceTrack.mNumPositionKeys;++key)
                    { const auto& value=sourceTrack.mPositionKeys[key]; track.positions.push_back({Time(value.mTime,frequency),{value.mValue.x,value.mValue.y,value.mValue.z}}); }
                    for (unsigned int key=0;key<sourceTrack.mNumScalingKeys;++key)
                    { const auto& value=sourceTrack.mScalingKeys[key]; track.scales.push_back({Time(value.mTime,frequency),{value.mValue.x,value.mValue.y,value.mValue.z}}); }
                    for (unsigned int key=0;key<sourceTrack.mNumRotationKeys;++key)
                    { const auto& value=sourceTrack.mRotationKeys[key]; track.rotations.push_back({Time(value.mTime,frequency),{value.mValue.x,value.mValue.y,value.mValue.z,value.mValue.w}}); }
                    clip.tracks[node->second]=std::move(track);
                }
                rig->clips.push_back(std::move(clip));
            }
            const auto settings=AssetDatabase::Read(path);
            rig->importScale=settings.scale;
            const auto assetRoot=AssetDatabase::Root(path);
            for (auto& mesh : rig->meshes) {
                if (settings.flipV) for (auto& vertex : mesh.mesh.vertices) vertex.uv[1]=1-vertex.uv[1];
                if (!assetRoot.empty() && !mesh.mesh.texturePath.empty()) mesh.mesh.texturePath=assetRoot/AssetDatabase(assetRoot).Resolve(std::filesystem::absolute(mesh.mesh.texturePath).lexically_relative(assetRoot));
            }
            const auto matrices=Matrices(*rig,Sample(*rig,"",0,false));
            for (const auto& mesh : rig->meshes) static_cast<void>(Skin(*rig,mesh,matrices));
            error.clear(); return rig;
        }
        catch (const std::exception& exception) { error=exception.what(); return {}; }
    }
}
