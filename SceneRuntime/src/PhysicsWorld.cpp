#include <SceneRuntime/PhysicsWorld.h>
#include <SceneRuntime/SceneTransforms.h>
#include <Engine/Graphics/Models/ModelLoader.h>
#include <Engine/Core/Log.h>
#pragma warning(push,0)
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/StateRecorderImpl.h>
#pragma warning(pop)
#include <cmath>
#include <mutex>
#include <set>
#include <stdexcept>
#include <cstdarg>
#include <cstdio>
#include <iterator>

namespace
{
    using namespace SceneRuntime;
    using Matrix=DirectX::XMFLOAT4X4;
    JPH::Vec3 V(const std::array<float,3>& p) { return {p[0],p[1],p[2]}; }
    std::array<float,3> A(JPH::Vec3Arg p) { return {p.GetX(),p.GetY(),p.GetZ()}; }
    bool Finite(const std::array<float,3>& value) { return std::all_of(value.begin(),value.end(),[](float f) { return std::isfinite(f); }); }
    void Register()
    {
        struct Registration
        {
            Registration()
            {
                JPH::RegisterDefaultAllocator();
                JPH::Trace=[](const char* format,...) { char buffer[2048]; va_list args; va_start(args,format); vsnprintf_s(buffer,sizeof(buffer),_TRUNCATE,format,args); va_end(args); Engine::Log::Warning(buffer); };
#ifdef JPH_ENABLE_ASSERTS
                JPH::AssertFailed=[](const char* expression,const char* message,const char* file,JPH::uint line) {
                    Engine::Log::Error(std::string("Jolt assertion: ")+expression+" "+(message ? message : "")+" "+file+":"+std::to_string(line)); return true;
                };
#endif
                JPH::Factory::sInstance=new JPH::Factory; JPH::RegisterTypes();
            }
            ~Registration() { JPH::UnregisterTypes(); delete JPH::Factory::sInstance; JPH::Factory::sInstance=nullptr; }
        };
        static Registration registration;
        static_cast<void>(registration);
    }
    struct Layers final : JPH::BroadPhaseLayerInterface
    {
        JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
        JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override { return JPH::BroadPhaseLayer(static_cast<JPH::uint8>(layer)); }
    };
    struct BroadFilter final : JPH::ObjectVsBroadPhaseLayerFilter
    { bool ShouldCollide(JPH::ObjectLayer a,JPH::BroadPhaseLayer b) const override { return a==1 || b==JPH::BroadPhaseLayer(1); } };
    struct PairFilter final : JPH::ObjectLayerPairFilter
    { bool ShouldCollide(JPH::ObjectLayer a,JPH::ObjectLayer b) const override { return a==1 || b==1; } };
    bool Related(const SceneLayout& scene,size_t a,size_t b)
    {
        const auto ancestor=[&](size_t child,size_t parent) {
            auto id=scene.objects[child].parentId;
            while (!id.empty())
            {
                if (id==scene.objects[parent].id) return true;
                const auto found=std::find_if(scene.objects.begin(),scene.objects.end(),[&](const auto& item) { return item.id==id; });
                if (found==scene.objects.end()) break;
                id=found->parentId;
            }
            return false;
        };
        return a==b || ancestor(a,b) || ancestor(b,a);
    }
    struct Pose { std::array<float,3> scale{1,1,1}; JPH::Vec3 position=JPH::Vec3::sZero(); JPH::Quat rotation=JPH::Quat::sIdentity(); };
    Pose ReadPose(const Matrix& matrix)
    {
        ScenePlacement reference,pose;
        if (!SceneTransforms::ReadTransform(matrix,reference,pose)) throw std::runtime_error("Physics requires a transform without inherited shear");
        DirectX::XMVECTOR scale,rotation,position;
        if (!DirectX::XMMatrixDecompose(&scale,&rotation,&position,DirectX::XMLoadFloat4x4(&matrix))) throw std::runtime_error("Invalid physics transform");
        DirectX::XMFLOAT4 q; DirectX::XMStoreFloat4(&q,rotation);
        return {pose.scale,V(pose.position),JPH::Quat(q.x,q.y,q.z,q.w)};
    }
    Matrix WorldMatrix(const Pose& pose)
    {
        Matrix result;
        const auto q=pose.rotation;
        DirectX::XMStoreFloat4x4(&result,DirectX::XMMatrixScaling(pose.scale[0],pose.scale[1],pose.scale[2])*
            DirectX::XMMatrixRotationQuaternion(DirectX::XMVectorSet(q.GetX(),q.GetY(),q.GetZ(),q.GetW()))*
            DirectX::XMMatrixTranslation(pose.position.GetX(),pose.position.GetY(),pose.position.GetZ()));
        return result;
    }
    JPH::RefConst<JPH::Shape> Checked(const JPH::Shape::ShapeResult& result)
    { if (result.HasError()) throw std::runtime_error(result.GetError().c_str()); return result.Get(); }
    JPH::RefConst<JPH::Shape> Shape(const ScenePlacement& object,const Pose& pose,const std::filesystem::path& root)
    {
        const auto& c=*object.boxCollider;
        JPH::RefConst<JPH::Shape> shape;
        const auto scale=V(pose.scale).Abs();
        if (c.shape=="box") shape=Checked(JPH::BoxShapeSettings(V(c.size)*scale*0.5f,0).Create());
        else if (c.shape=="sphere") shape=Checked(JPH::SphereShapeSettings(c.radius*scale.ReduceMax()).Create());
        else if (c.shape=="capsule") shape=c.halfHeight==0 ? Checked(JPH::SphereShapeSettings(c.radius*scale.ReduceMax()).Create()) : Checked(JPH::CapsuleShapeSettings(c.halfHeight*scale.GetY(),c.radius*std::max(scale.GetX(),scale.GetZ())).Create());
        else
        {
            const auto model=c.model.empty() ? object.Model() : c.model;
            if (model.empty()) throw std::runtime_error("Mesh collider requires a model");
            std::vector<Engine::MeshData> meshes;
            if (!Engine::ModelLoader::Load(root/model,meshes)) throw std::runtime_error("Mesh collider model cannot be loaded");
            JPH::TriangleList triangles;
            JPH::Array<JPH::Vec3> points;
            for (const auto& mesh : meshes) for (size_t i=0;i+2<mesh.indices.size();i+=3)
            {
                const auto vertex=[&](size_t index) { return V(mesh.vertices.at(mesh.indices.at(index)).position)*V(pose.scale); };
                auto a=vertex(i),b=vertex(i+1),d=vertex(i+2);
                const auto& va=mesh.vertices.at(mesh.indices.at(i)); const auto& vb=mesh.vertices.at(mesh.indices.at(i+1)); const auto& vd=mesh.vertices.at(mesh.indices.at(i+2));
                const auto facing=(V(vb.position)-V(va.position)).Cross(V(vd.position)-V(va.position)).Dot(V(va.normal));
                if ((facing<0)!=(pose.scale[0]*pose.scale[1]*pose.scale[2]<0)) std::swap(b,d);
                triangles.emplace_back(a,b,d); points.push_back(a); points.push_back(b); points.push_back(d);
            }
            if (!c.convex && object.rigidBody && object.rigidBody->enabled && object.rigidBody->motion=="dynamic") throw std::runtime_error("Dynamic mesh colliders require Convex");
            shape=c.convex ? Checked(JPH::ConvexHullShapeSettings(points).Create()) : Checked(JPH::MeshShapeSettings(triangles).Create());
        }
        return Checked(JPH::RotatedTranslatedShapeSettings(V(c.center)*V(pose.scale),JPH::Quat::sIdentity(),shape).Create());
    }
    Pose ColliderPose(const ScenePlacement& object,const Matrix& matrix,bool& baked)
    {
        try { baked=false; return ReadPose(matrix); }
        catch (const std::exception&)
        {
            if ((object.rigidBody && object.rigidBody->enabled) || (object.playerController && object.playerController->enabled && object.playerController->usePhysics)) throw;
            baked=true; Pose pose; pose.position={matrix._41,matrix._42,matrix._43}; return pose;
        }
    }
    JPH::RefConst<JPH::Shape> BakedShape(const ScenePlacement& object,const Matrix& matrix,const std::filesystem::path& root)
    {
        const auto& c=*object.boxCollider;
        const auto transform=[&](std::array<float,3> point) {
            for (size_t i=0;i<3;++i) point[i]+=c.center[i];
            DirectX::XMFLOAT3 output;
            DirectX::XMStoreFloat3(&output,DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(point[0],point[1],point[2],0),DirectX::XMLoadFloat4x4(&matrix)));
            return JPH::Vec3(output.x,output.y,output.z);
        };
        JPH::Array<JPH::Vec3> points;
        if (c.shape=="box")
            for (int corner=0;corner<8;++corner) points.push_back(transform({c.size[0]*((corner&1) ? 0.5f : -0.5f),c.size[1]*((corner&2) ? 0.5f : -0.5f),c.size[2]*((corner&4) ? 0.5f : -0.5f)}));
        else if (c.shape=="sphere" || c.shape=="capsule")
            for (int ring=0;ring<=16;++ring) for (int side=0;side<32;++side)
            {
                const float latitude=JPH::JPH_PI*(static_cast<float>(ring)/16-0.5f),longitude=2*JPH::JPH_PI*side/32;
                float y=std::sin(latitude)*c.radius;
                if (c.shape=="capsule") y+=ring<8 ? -c.halfHeight : c.halfHeight;
                points.push_back(transform({std::cos(latitude)*std::cos(longitude)*c.radius,y,std::cos(latitude)*std::sin(longitude)*c.radius}));
            }
        else
        {
            std::vector<Engine::MeshData> meshes;
            if (!Engine::ModelLoader::Load(root/(c.model.empty() ? object.Model() : c.model),meshes)) throw std::runtime_error("Cannot load sheared collider model");
            JPH::TriangleList triangles;
            for (const auto& mesh : meshes) for (size_t i=0;i+2<mesh.indices.size();i+=3)
            {
                const auto& a=mesh.vertices.at(mesh.indices.at(i)); const auto& b=mesh.vertices.at(mesh.indices.at(i+1)); const auto& d=mesh.vertices.at(mesh.indices.at(i+2));
                auto p=transform(a.position),q=transform(b.position),r=transform(d.position);
                const auto facing=(V(b.position)-V(a.position)).Cross(V(d.position)-V(a.position)).Dot(V(a.normal));
                const bool mirrored=DirectX::XMVectorGetX(DirectX::XMMatrixDeterminant(DirectX::XMLoadFloat4x4(&matrix)))<0;
                if ((facing<0)!=mirrored) std::swap(q,r);
                triangles.emplace_back(p,q,r); points.push_back(p); points.push_back(q); points.push_back(r);
            }
            if (!c.convex) return Checked(JPH::MeshShapeSettings(triangles).Create());
        }
        return Checked(JPH::ConvexHullShapeSettings(points,0).Create());
    }
    using Contact=std::tuple<std::string,std::string,bool>;
}

namespace SceneRuntime
{
    struct PhysicsWorld::Impl final : JPH::ContactListener
    {
        struct Entry { size_t index=0; JPH::BodyID body; JPH::Ref<JPH::CharacterVirtual> character; Pose pose; bool baked=false; Matrix basis{}; };
        Layers layers; BroadFilter broadFilter; PairFilter pairFilter;
        JPH::TempAllocatorImpl temporary{16*1024*1024};
        JPH::JobSystemSingleThreaded jobs{JPH::cMaxPhysicsJobs};
        JPH::PhysicsSystem system;
        SceneLayout definition;
        std::map<std::string,Entry> entries;
        std::map<JPH::uint32,size_t> bodyObjects;
        std::set<Contact> contacts;
        explicit Impl(const SceneLayout& layout) : definition(layout)
        { system.Init(8192,0,32768,32768,layers,broadFilter,pairFilter); system.SetGravity({0,-20,0}); system.SetContactListener(this); }
        ~Impl()
        {
            for (auto& [id,entry] : entries)
            {
                static_cast<void>(id);
                if (entry.character) entry.character=nullptr;
                else if (!entry.body.IsInvalid()) { system.GetBodyInterface().RemoveBody(entry.body); system.GetBodyInterface().DestroyBody(entry.body); }
            }
        }
        bool Allowed(size_t a,size_t b) const
        {
            const auto& x=*definition.objects[a].boxCollider; const auto& y=*definition.objects[b].boxCollider;
            return (x.mask&(1u<<y.layer)) && (y.mask&(1u<<x.layer)) && !Related(definition,a,b);
        }
        JPH::ValidateResult OnContactValidate(const JPH::Body& a,const JPH::Body& b,JPH::RVec3Arg,const JPH::CollideShapeResult&) override
        { return Allowed(static_cast<size_t>(a.GetUserData()),static_cast<size_t>(b.GetUserData())) ? JPH::ValidateResult::AcceptAllContactsForThisBodyPair : JPH::ValidateResult::RejectAllContactsForThisBodyPair; }
        struct Filter final : JPH::BodyFilter
        {
            const Impl& world; size_t actor; unsigned int mask; bool sensors; std::string ignore;
            Filter(const Impl& w,size_t a,unsigned int m,bool s,std::string id={}) : world(w),actor(a),mask(m),sensors(s),ignore(std::move(id)) {}
            bool ShouldCollide(const JPH::BodyID& body) const override
            {
                const auto found=world.bodyObjects.find(body.GetIndexAndSequenceNumber());
                if (found==world.bodyObjects.end()) return false;
                const auto& object=world.definition.objects[found->second]; const auto& c=*object.boxCollider;
                return object.id!=ignore && (mask&(1u<<c.layer)) && (sensors || !c.isTrigger) &&
                    (actor==SIZE_MAX || world.Allowed(actor,found->second));
            }
        };
        void Build(const std::vector<Matrix>& matrices,const std::filesystem::path& root,const Impl* old,const ScenePhysics::States& states)
        {
            if (std::count_if(definition.objects.begin(),definition.objects.end(),[](const auto& object) { return object.boxCollider && object.boxCollider->enabled; })>8192) throw std::runtime_error("Physics body limit exceeded");
            for (size_t i=0;i<definition.objects.size();++i)
            {
                const auto& object=definition.objects[i];
                if (!object.boxCollider || !object.boxCollider->enabled)
                {
                    if ((object.rigidBody && object.rigidBody->enabled) || (object.playerController && object.playerController->enabled && object.playerController->usePhysics))
                        throw std::runtime_error("Rigid body or physics player requires an enabled collider: "+object.id);
                    continue;
                }
                Entry entry; entry.index=i; entry.pose=ColliderPose(object,matrices[i],entry.baked); entry.basis=matrices[i];
                const auto shape=entry.baked ? BakedShape(object,matrices[i],root) : Shape(object,entry.pose,root);
                const bool controller=object.playerController && object.playerController->enabled;
                const bool body=object.rigidBody && object.rigidBody->enabled;
                if (controller && object.playerController->usePhysics)
                {
                    if (body || object.boxCollider->isTrigger || (object.boxCollider->shape=="mesh" && !object.boxCollider->convex)) throw std::runtime_error("Physics player requires a solid convex collider and no RigidBody");
                    JPH::CharacterVirtualSettings settings; settings.mShape=shape; settings.mInnerBodyShape=shape; settings.mInnerBodyLayer=1;
                    settings.mMaxSlopeAngle=object.playerController->maxSlopeDegrees*JPH::JPH_PI/180;
                    settings.mCharacterPadding=0.001f; settings.mPredictiveContactDistance=0.02f;
                    entry.character=new JPH::CharacterVirtual(&settings,entry.pose.position,entry.pose.rotation,i,&system);
                    entry.body=entry.character->GetInnerBodyID();
                    const auto state=states.find(object.id);
                    if (state!=states.end()) entry.character->SetLinearVelocity({0,state->second.verticalSpeed,0});
                }
                else
                {
                    const auto motion=body ? (object.rigidBody->motion=="dynamic" ? JPH::EMotionType::Dynamic : JPH::EMotionType::Kinematic) :
                        (controller ? JPH::EMotionType::Kinematic : JPH::EMotionType::Static);
                    JPH::BodyCreationSettings settings(shape,entry.pose.position,entry.pose.rotation,motion,motion==JPH::EMotionType::Static ? 0 : 1);
                    settings.mUserData=i; settings.mIsSensor=object.boxCollider->isTrigger; settings.mCollideKinematicVsNonDynamic=true;
                    if (body)
                    {
                        const auto& rb=*object.rigidBody;
                        settings.mOverrideMassProperties=JPH::EOverrideMassProperties::CalculateInertia; settings.mMassPropertiesOverride.mMass=rb.mass;
                        settings.mFriction=rb.friction; settings.mRestitution=rb.restitution; settings.mGravityFactor=rb.gravityScale;
                        settings.mLinearDamping=rb.linearDamping; settings.mAngularDamping=rb.angularDamping;
                        settings.mMaxLinearVelocity=100000; settings.mMaxAngularVelocity=100000;
                        settings.mLinearVelocity=V(rb.velocity); settings.mAngularVelocity=V(rb.angularVelocity);
                        settings.mMotionQuality=rb.continuous ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
                        if (old)
                        {
                            const auto found=old->entries.find(object.id);
                            if (found!=old->entries.end() && old->definition.objects[found->second.index].rigidBody==object.rigidBody)
                            {
                                settings.mLinearVelocity=old->system.GetBodyInterface().GetLinearVelocity(found->second.body);
                                settings.mAngularVelocity=old->system.GetBodyInterface().GetAngularVelocity(found->second.body);
                            }
                        }
                    }
                    entry.body=system.GetBodyInterface().CreateAndAddBody(settings,JPH::EActivation::Activate);
                    if (entry.body.IsInvalid()) throw std::runtime_error("Physics body creation failed");
                }
                bodyObjects[entry.body.GetIndexAndSequenceNumber()]=i; entries.emplace(object.id,std::move(entry));
            }
            system.OptimizeBroadPhase();
            if (old) contacts=old->contacts;
        }
        bool Matches(const SceneLayout& layout,const std::vector<Matrix>& matrices) const
        {
            if (layout.objects.size()!=definition.objects.size()) return false;
            for (size_t i=0;i<layout.objects.size();++i)
            {
                const auto& a=layout.objects[i]; const auto& b=definition.objects[i];
                if (a.id!=b.id || a.parentId!=b.parentId || a.boxCollider!=b.boxCollider || a.rigidBody!=b.rigidBody || a.playerController!=b.playerController) return false;
                if (a.boxCollider && a.boxCollider->enabled)
                {
                    bool baked=false; const auto scale=ColliderPose(a,matrices[i],baked).scale;
                    const auto& entry=entries.at(a.id); const auto& stored=entry.pose.scale;
                    if (baked!=entry.baked) return false;
                    if (baked) for (size_t row=0;row<3;++row) for (size_t column=0;column<3;++column)
                        if (matrices[i].m[row][column]!=entry.basis.m[row][column]) return false;
                    for (size_t axis=0;axis<3;++axis) if (std::abs(scale[axis]-stored[axis])>1e-5f*std::max(1.0f,std::abs(stored[axis]))) return false;
                }
            }
            return true;
        }
        void Gather(std::vector<PhysicsContactEvent>& events)
        {
            std::set<Contact> current;
            for (const auto& [id,entry] : entries)
            {
                const auto& object=definition.objects[entry.index];
                if (!entry.character && system.GetBodyInterface().GetMotionType(entry.body)==JPH::EMotionType::Static) continue;
                JPH::RefConst<JPH::Shape> shape;
                auto transform=JPH::RMat44::sIdentity();
                {
                    JPH::BodyLockRead lock(system.GetBodyLockInterface(),entry.body);
                    if (!lock.Succeeded()) continue;
                    shape=lock.GetBody().GetShape(); transform=lock.GetBody().GetCenterOfMassTransform();
                }
                JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
                JPH::CollideShapeSettings settings; settings.mMaxSeparationDistance=0.003f;
                system.GetNarrowPhaseQuery().CollideShape(shape,JPH::Vec3::sOne(),transform,settings,JPH::RVec3::sZero(),collector,{}, {},Filter(*this,entry.index,0xffffffffu,true));
                for (const auto& hit : collector.mHits)
                {
                    const auto other=bodyObjects.find(hit.mBodyID2.GetIndexAndSequenceNumber()); if (other==bodyObjects.end()) continue;
                    const auto& b=definition.objects[other->second];
                    const bool trigger=object.boxCollider->isTrigger || b.boxCollider->isTrigger;
                    current.emplace(std::min(id,b.id),std::max(id,b.id),trigger);
                }
            }
            for (const auto& pair : current) if (!contacts.contains(pair)) events.push_back({std::get<0>(pair),std::get<1>(pair),"Enter",std::get<2>(pair)});
            for (const auto& pair : contacts) if (!current.contains(pair)) events.push_back({std::get<0>(pair),std::get<1>(pair),"Exit",std::get<2>(pair)});
            contacts=std::move(current);
        }
        void Step(SceneLayout& layout,ScenePhysics::States& states,const std::vector<Matrix>& matrices,float seconds,float horizontal,float vertical,bool jump,
            const std::map<std::string,std::array<float,3>>& impulses,std::vector<PhysicsContactEvent>& events)
        {
            auto& bodies=system.GetBodyInterface();
            for (auto& [id,entry] : entries)
            {
                bool baked=false; auto pose=ColliderPose(layout.objects[entry.index],matrices[entry.index],baked);
                const bool moved=entry.baked ? !pose.position.IsClose(entry.pose.position) : !SceneTransforms::Matches(WorldMatrix(entry.pose),matrices[entry.index]);
                if (entry.character)
                {
                    if (moved) { entry.character->SetPosition(pose.position); entry.character->SetRotation(pose.rotation); }
                }
                else if (bodies.GetMotionType(entry.body)==JPH::EMotionType::Kinematic)
                {
                    const auto& body=layout.objects[entry.index].rigidBody;
                    if (body && body->enabled)
                    {
                        pose.position+=V(body->velocity)*seconds;
                        const auto angular=V(body->angularVelocity);
                        if (angular.LengthSq()>1e-12f) pose.rotation=JPH::Quat::sRotation(angular.Normalized(),angular.Length()*seconds)*pose.rotation;
                    }
                    bodies.MoveKinematic(entry.body,pose.position,pose.rotation,seconds);
                }
                else if (moved) bodies.SetPositionAndRotation(entry.body,pose.position,pose.rotation,JPH::EActivation::Activate);
                const auto impulse=impulses.find(id);
                if (impulse!=impulses.end() && bodies.GetMotionType(entry.body)==JPH::EMotionType::Dynamic) bodies.AddImpulse(entry.body,V(impulse->second));
            }
            const int steps=std::max(1,static_cast<int>(std::ceil(seconds*120))); const float tick=seconds/static_cast<float>(steps);
            const float magnitude=std::max(1.0f,std::hypot(horizontal,vertical));
            for (int step=0;step<steps;++step)
            {
                for (auto& [id,entry] : entries) if (entry.character)
                {
                    const auto& object=layout.objects[entry.index]; const auto& controller=*object.playerController;
                    auto& character=*entry.character;
                    character.UpdateGroundVelocity();
                    auto& state=states[id]; state.grounded=character.GetGroundState()==JPH::CharacterBase::EGroundState::OnGround;
                    const auto ground=state.grounded ? character.GetGroundVelocity() : JPH::Vec3::sZero();
                    float vy=character.GetLinearVelocity().GetY();
                    if (state.grounded && vy<=ground.GetY()+0.1f) vy=ground.GetY();
                    if (step==0 && jump && state.grounded && controller.useGravity) { vy=controller.jumpSpeed+ground.GetY(); state.grounded=false; }
                    if (controller.useGravity) vy-=controller.gravity*tick; else vy=0;
                    auto parent=DirectX::XMMatrixIdentity();
                    if (!object.parentId.empty())
                    {
                        const auto found=std::find_if(layout.objects.begin(),layout.objects.end(),[&](const auto& item) { return item.id==object.parentId; });
                        parent=DirectX::XMLoadFloat4x4(&matrices[static_cast<size_t>(found-layout.objects.begin())]);
                    }
                    DirectX::XMFLOAT3 movement;
                    DirectX::XMStoreFloat3(&movement,DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(horizontal/magnitude*controller.moveSpeed,0,vertical/magnitude*controller.moveSpeed,0),parent));
                    character.SetLinearVelocity({movement.x+ground.GetX(),vy+movement.y,movement.z+ground.GetZ()});
                    JPH::CharacterVirtual::ExtendedUpdateSettings settings;
                    settings.mWalkStairsStepUp={0,controller.stepHeight,0}; settings.mStickToFloorStepDown={0,controller.useGravity ? -0.05f : 0,0};
                    character.ExtendedUpdate(tick,{0,controller.useGravity ? -controller.gravity : 0,0},settings,
                        system.GetDefaultBroadPhaseLayerFilter(1),system.GetDefaultLayerFilter(1),Filter(*this,entry.index,0xffffffffu,false),{},temporary);
                    state.verticalSpeed=character.GetLinearVelocity().GetY(); state.grounded=character.GetGroundState()==JPH::CharacterBase::EGroundState::OnGround;
                }
                if (system.Update(tick,1,&temporary,&jobs)!=JPH::EPhysicsUpdateError::None) throw std::runtime_error("Physics capacity exceeded");
                Gather(events);
            }
            std::transform(contacts.begin(),contacts.end(),std::back_inserter(events),[](const auto& pair) {
                return PhysicsContactEvent{std::get<0>(pair),std::get<1>(pair),"Stay",std::get<2>(pair)};
            });
            std::map<std::string,Matrix> physical;
            for (auto& [id,entry] : entries)
            {
                entry.pose.position=entry.character ? JPH::Vec3(entry.character->GetPosition()) : JPH::Vec3(bodies.GetPosition(entry.body));
                entry.pose.rotation=entry.character ? entry.character->GetRotation() : bodies.GetRotation(entry.body);
                const auto& rb=layout.objects[entry.index].rigidBody;
                if (entry.character || (rb && rb->enabled)) physical[id]=WorldMatrix(entry.pose);
            }
            std::map<std::string,Matrix> resolved;
            const auto world=[&](auto&& self,const std::string& id)->Matrix {
                if (resolved.contains(id)) return resolved.at(id);
                if (physical.contains(id)) return resolved[id]=physical.at(id);
                const auto found=std::find_if(layout.objects.begin(),layout.objects.end(),[&](const auto& item) { return item.id==id; });
                Matrix local{}; if (found==layout.objects.end() || !SceneTransforms::Compose(*found,local)) throw std::runtime_error("Invalid physics parent transform");
                if (!found->parentId.empty()) { const auto p=self(self,found->parentId); DirectX::XMStoreFloat4x4(&local,DirectX::XMLoadFloat4x4(&local)*DirectX::XMLoadFloat4x4(&p)); }
                return resolved[id]=local;
            };
            for (auto& object : layout.objects) if (physical.contains(object.id))
            {
                Matrix local=physical.at(object.id);
                if (!object.parentId.empty() && !SceneTransforms::WorldToLocal(local,world(world,object.parentId),local)) throw std::runtime_error("Physics parent conversion failed");
                ScenePlacement placement;
                if (!SceneTransforms::ReadTransform(local,object,placement)) throw std::runtime_error("Physics cannot save inherited shear");
                object.position=placement.position; object.rotation=placement.rotation; object.scale=placement.scale;
            }
        }
    };
    PhysicsWorld::PhysicsWorld() { Register(); }
    PhysicsWorld::~PhysicsWorld()=default;
    void PhysicsWorld::Reset() { impl_.reset(); impulses_.clear(); events_.clear(); }
    bool PhysicsWorld::AddImpulse(const std::string& id,const std::array<float,3>& impulse)
    {
        if (!Finite(impulse) || (!impulses_.contains(id) && impulses_.size()>=4096)) return false;
        auto value=impulses_[id]; for (size_t i=0;i<3;++i) value[i]+=impulse[i];
        if (!Finite(value) || std::any_of(value.begin(),value.end(),[](float f) { return std::abs(f)>1000000; })) return false;
        impulses_[id]=value; return true;
    }
    bool PhysicsWorld::Advance(SceneLayout& layout,ScenePhysics::States& states,double seconds,float horizontal,float vertical,bool jump,
        const std::filesystem::path& root,std::string& error)
    {
        if (!std::isfinite(seconds) || seconds<=0 || !std::isfinite(horizontal) || !std::isfinite(vertical)) { error="Invalid physics input"; return false; }
        std::vector<Matrix> matrices;
        if (!SceneTransforms::Resolve(layout,matrices,error)) return false;
        JPH::StateRecorderImpl snapshot;
        std::map<std::string,JPH::StateRecorderImpl> characters;
        std::set<Contact> previousContacts;
        std::map<std::string,Pose> previousPoses;
        bool saved=false;
        try
        {
            auto candidate=layout; auto candidateStates=states;
            std::unique_ptr<Impl> replacement;
            auto* activeWorld=impl_.get();
            if (!activeWorld || !activeWorld->Matches(layout,matrices))
            {
                static_cast<void>(layout.Serialize()); replacement=std::make_unique<Impl>(layout);
                replacement->Build(matrices,root,impl_.get(),states); activeWorld=replacement.get();
            }
            auto& world=*activeWorld;
            if (!replacement)
            {
                world.system.SaveState(snapshot); previousContacts=world.contacts;
                for (const auto& [id,entry] : world.entries) { previousPoses[id]=entry.pose; if (entry.character) entry.character->SaveState(characters[id]); }
                saved=true;
            }
            std::vector<PhysicsContactEvent> events;
            world.Step(candidate,candidateStates,matrices,static_cast<float>(std::min(seconds,0.1)),std::clamp(horizontal,-1.0f,1.0f),std::clamp(vertical,-1.0f,1.0f),jump,impulses_,events);
            if (!SceneTransforms::Resolve(candidate,matrices,error)) throw std::runtime_error(error);
            if (replacement) impl_=std::move(replacement);
            layout=std::move(candidate); states=std::move(candidateStates); events_=std::move(events); impulses_.clear(); error.clear(); return true;
        }
        catch (const std::exception& exception)
        {
            if (saved)
            {
                snapshot.Rewind(); impl_->system.RestoreState(snapshot); impl_->contacts=std::move(previousContacts);
                for (auto& [id,entry] : impl_->entries) { entry.pose=previousPoses.at(id); if (entry.character) { characters.at(id).Rewind(); entry.character->RestoreState(characters.at(id)); } }
            }
            error=exception.what(); return false;
        }
    }
    std::optional<PhysicsRayHit> PhysicsWorld::Raycast(const std::array<float,3>& origin,const std::array<float,3>& direction,float distance,unsigned int mask,bool includeTriggers,const std::string& ignore) const
    {
        if (!impl_ || !Finite(origin) || !Finite(direction) || !std::isfinite(distance) || distance<=0 || distance>100000) return {};
        auto vector=V(direction); if (vector.LengthSq()<1e-12f) return {};
        const JPH::RRayCast ray(V(origin),vector.Normalized()*distance); JPH::RayCastResult hit;
        if (!impl_->system.GetNarrowPhaseQuery().CastRay(ray,hit,{}, {},Impl::Filter(*impl_,SIZE_MAX,mask,includeTriggers,ignore))) return {};
        JPH::BodyLockRead lock(impl_->system.GetBodyLockInterface(),hit.mBodyID);
        if (!lock.Succeeded()) return {};
        const auto& object=impl_->definition.objects.at(static_cast<size_t>(lock.GetBody().GetUserData()));
        const auto position=ray.GetPointOnRay(hit.mFraction);
        return PhysicsRayHit{object.id,distance*hit.mFraction,A(JPH::Vec3(position)),A(lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2,position)),object.boxCollider->isTrigger};
    }
}
