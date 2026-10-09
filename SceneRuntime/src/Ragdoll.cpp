#include <SceneRuntime/Ragdoll.h>
#include <SceneRuntime/SceneTransforms.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace
{
    using namespace SceneRuntime;
    using Matrix=DirectX::XMFLOAT4X4;
    size_t Index(const SceneLayout& layout,const std::string& id)
    {
        const auto found=std::find_if(layout.objects.begin(),layout.objects.end(),[&](const auto& p){return p.id==id;});
        if(found==layout.objects.end()) throw std::runtime_error("Ragdoll body missing: "+id);
        return static_cast<size_t>(found-layout.objects.begin());
    }
    size_t BoneIndex(const Engine::SkeletonData& rig,const std::string& name)
    {
        const auto found=std::find_if(rig.nodes.begin(),rig.nodes.end(),[&](const auto& bone){return bone.name==name;});
        if(found==rig.nodes.end()) throw std::runtime_error("Ragdoll bone missing: "+name);
        if(std::count_if(rig.nodes.begin(),rig.nodes.end(),[&](const auto& bone){return bone.name==name;})!=1) throw std::runtime_error("Ragdoll bone name is ambiguous: "+name);
        return static_cast<size_t>(found-rig.nodes.begin());
    }
    Matrix Offset(const RagdollBone& bone) {Matrix result; std::memcpy(&result,bone.bindOffset.data(),sizeof(result)); return result;}
    DirectX::XMMATRIX Import(const Engine::SkeletonData& rig)
    { return DirectX::XMLoadFloat4x4(&rig.inverseRoot)*DirectX::XMMatrixScaling(rig.importScale,rig.importScale,rig.importScale); }
    std::vector<Engine::BonePose> BasePose(const Engine::SkeletonData& rig,const std::vector<Engine::BonePose>& pose)
    {
        if(pose.size()==rig.nodes.size()) return pose;
        std::vector<Engine::BonePose> result; for(const auto& bone:rig.nodes) result.push_back(bone.rest); return result;
    }
    std::vector<Matrix> World(const SceneLayout& layout)
    {
        std::vector<Matrix> matrices; std::string error;
        if(!SceneTransforms::Resolve(layout,matrices,error)) throw std::runtime_error(error); return matrices;
    }
    std::string Unique(const SceneLayout& layout,const std::string& base)
    {
        for(size_t i=1;;++i) {const auto id=base+"-"+std::to_string(i); if(std::none_of(layout.objects.begin(),layout.objects.end(),[&](const auto& p){return p.id==id;})) return id;}
    }
}
namespace SceneRuntime
{
    void RagdollComponent::Validate() const
    {
        if(!std::isfinite(weight) || weight<0 || weight>1 || !std::isfinite(bodyMass) || bodyMass<=0 || bodyMass>100000 ||
            !std::isfinite(radius) || radius<=0 || radius>100 || !std::isfinite(swing) || swing<0 || swing>180 || !std::isfinite(twist) || twist<0 || twist>180 || bones.size()>256)
            throw std::runtime_error("Invalid Ragdoll settings");
        std::set<std::string> names,bodies;
        for(const auto& bone:bones) {
            if(bone.bone.empty() || bone.body.empty() || bone.bone.size()>128 || bone.body.size()>256 || bone.bone.find('\0')!=std::string::npos || bone.body.find('\0')!=std::string::npos || !names.insert(bone.bone).second || !bodies.insert(bone.body).second)
                throw std::runtime_error("Invalid or duplicate Ragdoll binding");
            for(const auto value:bone.bindOffset) if(!std::isfinite(value) || std::abs(value)>100000) throw std::runtime_error("Invalid Ragdoll bind offset");
            if(!SceneTransforms::IsUsable(Offset(bone))) throw std::runtime_error("Singular Ragdoll bind offset");
        }
    }
    void Ragdoll::Validate(const SceneLayout& layout,const ScenePlacement& owner,const Engine::SkeletonData& rig)
    {
        if(!owner.ragdoll) return;
        owner.ragdoll->Validate();
        if(!owner.animator || !owner.meshRenderer || rig.nodes.empty()) throw std::runtime_error("Ragdoll requires an Animator and skeleton model");
        for(const auto& binding:owner.ragdoll->bones) {
            BoneIndex(rig,binding.bone); const auto& body=layout.objects.at(Index(layout,binding.body));
            if(body.id==owner.id || !body.rigidBody || !body.boxCollider) throw std::runtime_error("Ragdoll binding requires a separate Collider and RigidBody");
        }
    }
    bool Ragdoll::Generate(SceneLayout& layout,const std::string& ownerId,const Engine::SkeletonData& rig,const std::vector<Engine::BonePose>& current,std::string& error)
    {
        try {
            auto candidate=layout; const auto ownerIndex=Index(candidate,ownerId); auto owner=candidate.objects[ownerIndex];
            if(!owner.ragdoll || !owner.ragdoll->bones.empty()) throw std::runtime_error("Add an empty Ragdoll before generating bodies");
            Validate(candidate,owner,rig); const auto& settings=*owner.ragdoll;
            const auto pose=BasePose(rig,current); const auto boneMatrices=Engine::Skeleton::Matrices(rig,pose);
            std::set<size_t> selected;
            for(const auto& mesh:rig.meshes) for(const auto joint:mesh.joints) selected.insert(joint);
            if(selected.empty()) for(size_t i=0;i<rig.nodes.size();++i) if(!rig.nodes[i].name.empty()) selected.insert(i);
            if(selected.empty() || selected.size()>256) throw std::runtime_error("Ragdoll requires between 1 and 256 named bones");
            std::map<size_t,std::string> bodies; std::map<size_t,Matrix> bodyMatrices;
            for(const auto bone:selected) {
                BoneIndex(rig,rig.nodes.at(bone).name);
                Matrix model; DirectX::XMStoreFloat4x4(&model,DirectX::XMLoadFloat4x4(&boneMatrices.at(bone))*Import(rig));
                ScenePlacement body; body.id=Unique(candidate,ownerId+"-bone"); body.name=rig.nodes[bone].name+" (Ragdoll)"; body.parentId=ownerId;
                ScenePlacement decomposed; if(!SceneTransforms::ReadTransform(model,body,decomposed)) throw std::runtime_error("Ragdoll bone transform contains shear");
                body.position=decomposed.position; body.rotation=decomposed.rotation;
                Matrix bodyMatrix; if(!SceneTransforms::Compose(body,bodyMatrix)) throw std::runtime_error("Invalid Ragdoll body transform");
                RagdollBone binding; binding.bone=rig.nodes[bone].name; binding.body=body.id;
                Matrix offset; DirectX::XMStoreFloat4x4(&offset,DirectX::XMLoadFloat4x4(&model)*DirectX::XMMatrixInverse(nullptr,DirectX::XMLoadFloat4x4(&bodyMatrix)));
                std::memcpy(binding.bindOffset.data(),&offset,sizeof(offset)); owner.ragdoll->bones.push_back(binding);
                body.rigidBody.emplace(); body.rigidBody->mass=settings.bodyMass; body.rigidBody->motion=settings.active && settings.enabled?"dynamic":"kinematic";
                body.rigidBody->angularDamping=.2f; body.boxCollider.emplace(); body.boxCollider->size={settings.radius*2,settings.radius*2,settings.radius*2};
                const auto child=std::find_if(selected.begin(),selected.end(),[&](size_t index){return rig.nodes[index].parent==static_cast<int>(bone);});
                if(child!=selected.end()) {
                    Matrix childMatrix; DirectX::XMStoreFloat4x4(&childMatrix,DirectX::XMLoadFloat4x4(&boneMatrices[*child])*Import(rig));
                    const auto point=DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(childMatrix._41,childMatrix._42,childMatrix._43,1),DirectX::XMMatrixInverse(nullptr,DirectX::XMLoadFloat4x4(&bodyMatrix)));
                    DirectX::XMFLOAT3 local; DirectX::XMStoreFloat3(&local,point);
                    body.boxCollider->center={local.x*.5f,local.y*.5f,local.z*.5f}; body.boxCollider->size={std::max(settings.radius*2,std::abs(local.x)),std::max(settings.radius*2,std::abs(local.y)),std::max(settings.radius*2,std::abs(local.z))};
                }
                bodies[bone]=body.id; bodyMatrices[bone]=bodyMatrix; candidate.objects.push_back(std::move(body));
            }
            for(const auto bone:selected) {
                int parent=rig.nodes[bone].parent; while(parent>=0 && !selected.contains(static_cast<size_t>(parent))) parent=rig.nodes[static_cast<size_t>(parent)].parent;
                if(parent<0) continue;
                auto& body=candidate.objects[Index(candidate,bodies.at(bone))]; body.joint.emplace(); auto& joint=*body.joint;
                joint.type="swingTwist"; joint.target=bodies.at(static_cast<size_t>(parent)); joint.enabled=settings.enabled && settings.active; joint.swing=settings.swing; joint.minimum=-settings.twist; joint.maximum=settings.twist;
                const auto& matrix=bodyMatrices.at(bone);
                const auto anchor=DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(matrix._41,matrix._42,matrix._43,1),DirectX::XMMatrixInverse(nullptr,DirectX::XMLoadFloat4x4(&bodyMatrices.at(static_cast<size_t>(parent)))));
                DirectX::XMFLOAT3 local; DirectX::XMStoreFloat3(&local,anchor); joint.connectedAnchor={local.x,local.y,local.z};
            }
            candidate.objects[ownerIndex].ragdoll=std::move(owner.ragdoll); static_cast<void>(candidate.Serialize());
            Validate(candidate,candidate.objects[ownerIndex],rig); layout=std::move(candidate); error.clear(); return true;
        } catch(const std::exception& exception) {error=exception.what(); return false;}
    }
    void Ragdoll::Synchronize(SceneLayout& layout,const std::string& ownerId,const Engine::SkeletonData& rig,const std::vector<Engine::BonePose>& current)
    {
        const auto ownerIndex=Index(layout,ownerId); const auto owner=layout.objects[ownerIndex]; if(!owner.ragdoll) return;
        Validate(layout,owner,rig); const auto matrices=World(layout); const auto bones=Engine::Skeleton::Matrices(rig,BasePose(rig,current));
        const bool active=owner.ragdoll->enabled && owner.ragdoll->active;
        for(const auto& binding:owner.ragdoll->bones) {
            auto& body=layout.objects[Index(layout,binding.body)]; body.rigidBody->motion=active?"dynamic":"kinematic";
            if(body.joint) body.joint->enabled=active;
            if(active) continue;
            body.rigidBody->velocity=body.rigidBody->angularVelocity={0,0,0}; const auto offset=Offset(binding);
            Matrix world; DirectX::XMStoreFloat4x4(&world,DirectX::XMMatrixInverse(nullptr,DirectX::XMLoadFloat4x4(&offset))*DirectX::XMLoadFloat4x4(&bones[BoneIndex(rig,binding.bone)])*Import(rig)*DirectX::XMLoadFloat4x4(&matrices[ownerIndex]));
            Matrix parent; DirectX::XMStoreFloat4x4(&parent,DirectX::XMMatrixIdentity()); if(!body.parentId.empty()) parent=matrices[Index(layout,body.parentId)];
            Matrix local; ScenePlacement transformed;
            if(!SceneTransforms::WorldToLocal(world,parent,local) || !SceneTransforms::ReadTransform(local,body,transformed)) throw std::runtime_error("Ragdoll body cannot follow bone transform");
            body.position=transformed.position; body.rotation=transformed.rotation; body.scale=transformed.scale;
        }
    }
    std::vector<Engine::BonePose> Ragdoll::Pose(const SceneLayout& layout,const ScenePlacement& owner,const Engine::SkeletonData& rig,const std::vector<Engine::BonePose>& current)
    {
        auto pose=BasePose(rig,current); if(!owner.ragdoll || !owner.ragdoll->enabled || !owner.ragdoll->active || owner.ragdoll->bones.empty()) return pose;
        Validate(layout,owner,rig); const auto matrices=World(layout); auto bones=Engine::Skeleton::Matrices(rig,pose);
        const auto toBone=DirectX::XMMatrixInverse(nullptr,DirectX::XMLoadFloat4x4(&matrices[Index(layout,owner.id)]))*DirectX::XMMatrixInverse(nullptr,Import(rig));
        std::set<size_t> overridden;
        for(const auto& binding:owner.ragdoll->bones) {
            const auto bone=BoneIndex(rig,binding.bone); const auto offset=Offset(binding);
            DirectX::XMStoreFloat4x4(&bones[bone],DirectX::XMLoadFloat4x4(&offset)*DirectX::XMLoadFloat4x4(&matrices[Index(layout,binding.body)])*toBone); overridden.insert(bone);
        }
        auto target=pose;
        for(size_t bone=0;bone<rig.nodes.size();++bone) {
            const auto parent=rig.nodes[bone].parent;
            if(!overridden.contains(bone)) {
                auto local=DirectX::XMMatrixScaling(pose[bone].scale[0],pose[bone].scale[1],pose[bone].scale[2])*DirectX::XMMatrixRotationQuaternion(DirectX::XMVectorSet(pose[bone].rotation[0],pose[bone].rotation[1],pose[bone].rotation[2],pose[bone].rotation[3]))*DirectX::XMMatrixTranslation(pose[bone].position[0],pose[bone].position[1],pose[bone].position[2]);
                if(parent>=0) local*=DirectX::XMLoadFloat4x4(&bones[static_cast<size_t>(parent)]); DirectX::XMStoreFloat4x4(&bones[bone],local); continue;
            }
            auto local=DirectX::XMLoadFloat4x4(&bones[bone]); if(parent>=0) local*=DirectX::XMMatrixInverse(nullptr,DirectX::XMLoadFloat4x4(&bones[static_cast<size_t>(parent)]));
            DirectX::XMVECTOR scale,rotation,position;
            if(!DirectX::XMMatrixDecompose(&scale,&rotation,&position,local)) throw std::runtime_error("Ragdoll pose cannot be decomposed");
            DirectX::XMFLOAT3 s,p; DirectX::XMFLOAT4 q; DirectX::XMStoreFloat3(&s,scale); DirectX::XMStoreFloat3(&p,position); DirectX::XMStoreFloat4(&q,DirectX::XMQuaternionNormalize(rotation));
            target[bone].scale={s.x,s.y,s.z}; target[bone].position={p.x,p.y,p.z}; target[bone].rotation={q.x,q.y,q.z,q.w};
        }
        return Engine::Skeleton::Blend(pose,target,owner.ragdoll->weight);
    }
}
