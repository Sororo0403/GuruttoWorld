#include <SceneRuntime/Physics/PhysicsWorld.h>
#include <Engine/Core/Profiler.h>
#include <SceneRuntime/Scene/SceneTransforms.h>
#include <SceneRuntime/Gameplay/GenreGeometry.h>
#include <Engine/Graphics/Models/ModelAssets.h>
#include <Engine/Core/Log.h>
#pragma warning(push, 0)
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyLockMulti.h>
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
#include "PhysicsJoints.h"
#include "PhysicsFrame.h"
#include <SceneRuntime/Scene/RuntimeSession.h>
#include <cmath>
#include <mutex>
#include <set>
#include <stdexcept>
#include <cstdarg>
#include <cstdio>
#include <iterator>
#include <cstring>
#include <fstream>
#include <Engine/Core/Json.h>
#include <Engine/Assets/AssetDatabase.h>
#include <Engine/Assets/ModelImportInputs.h>
#include <cctype>

namespace
{
    using namespace SceneRuntime;
    using Matrix = DirectX::XMFLOAT4X4;
    using QueryFiles = std::vector<Engine::ArtifactSources>;
    QueryFiles QueryGeometryFiles(const PhysicsFrame& layout, const std::filesystem::path& root)
    {
        QueryFiles files;
        for (const auto& object : layout.objects)
            if (object.boxCollider && object.boxCollider->enabled && object.boxCollider->shape == "mesh" &&
                !object.terrain && !object.tilemap)
            {
                const auto path =
                    root / (object.boxCollider->model.empty() ? object.Model() : object.boxCollider->model);
                files.push_back(Engine::ModelImportInputs::Capture(path, "collision-mesh").sources);
            }
        return files;
    }
    bool QueryFilesMatch(const QueryFiles& files)
    {
        return std::all_of(files.begin(), files.end(),
                           [](const auto& file)
                           {
                               return file.Unchanged();
                           });
    }
    JPH::Vec3 V(const std::array<float, 3>& p)
    {
        return {p[0], p[1], p[2]};
    }
    std::array<float, 3> A(JPH::Vec3Arg p)
    {
        return {p.GetX(), p.GetY(), p.GetZ()};
    }
    bool Finite(const std::array<float, 3>& value)
    {
        return std::all_of(value.begin(), value.end(),
                           [](float f)
                           {
                               return std::isfinite(f);
                           });
    }
    void Register()
    {
        struct Registration
        {
            Registration()
            {
                JPH::RegisterDefaultAllocator();
                JPH::Trace = [](const char* format, ...)
                {
                    char buffer[2048];
                    va_list args;
                    va_start(args, format);
                    vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, args);
                    va_end(args);
                    Engine::Log::Warning(buffer);
                };
#ifdef JPH_ENABLE_ASSERTS
                JPH::AssertFailed = [](const char* expression, const char* message, const char* file, JPH::uint line)
                {
                    Engine::Log::Error(std::string("Jolt assertion: ") + expression + " " + (message ? message : "") +
                                       " " + file + ":" + std::to_string(line));
                    return true;
                };
#endif
                JPH::Factory::sInstance = new JPH::Factory;
                JPH::RegisterTypes();
            }
            ~Registration()
            {
                JPH::UnregisterTypes();
                delete JPH::Factory::sInstance;
                JPH::Factory::sInstance = nullptr;
            }
        };
        static Registration registration;
        static_cast<void>(registration);
    }
    struct Layers final : JPH::BroadPhaseLayerInterface
    {
        JPH::uint GetNumBroadPhaseLayers() const override
        {
            return 2;
        }
        JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
        {
            return JPH::BroadPhaseLayer(static_cast<JPH::uint8>(layer));
        }
    };
    struct BroadFilter final : JPH::ObjectVsBroadPhaseLayerFilter
    {
        bool ShouldCollide(JPH::ObjectLayer a, JPH::BroadPhaseLayer b) const override
        {
            return a == 1 || b == JPH::BroadPhaseLayer(1);
        }
    };
    struct PairFilter final : JPH::ObjectLayerPairFilter
    {
        bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
        {
            return a == 1 || b == 1;
        }
    };
    bool Related(const PhysicsFrame& scene, size_t a, size_t b)
    {
        const auto ancestor = [&](size_t child, size_t parent)
        {
            auto id = scene.objects[child].parentId;
            while (!id.empty())
            {
                if (id == scene.objects[parent].id)
                    return true;
                const auto found = std::find_if(scene.objects.begin(), scene.objects.end(),
                                                [&](const auto& item)
                                                {
                                                    return item.id == id;
                                                });
                if (found == scene.objects.end())
                    break;
                id = found->parentId;
            }
            return false;
        };
        return a == b || ancestor(a, b) || ancestor(b, a);
    }
    struct Pose
    {
        std::array<float, 3> scale{1, 1, 1};
        JPH::Vec3 position = JPH::Vec3::sZero();
        JPH::Quat rotation = JPH::Quat::sIdentity();
    };
    Pose ReadPose(const Matrix& matrix)
    {
        PhysicsObject reference, pose;
        if (!PhysicsTransforms::ReadTransform(matrix, reference, pose))
            throw std::runtime_error("Physics requires a transform without inherited shear");
        DirectX::XMVECTOR scale, rotation, position;
        if (!DirectX::XMMatrixDecompose(&scale, &rotation, &position, DirectX::XMLoadFloat4x4(&matrix)))
            throw std::runtime_error("Invalid physics transform");
        DirectX::XMFLOAT4 q;
        DirectX::XMStoreFloat4(&q, rotation);
        return {pose.scale, V(pose.position), JPH::Quat(q.x, q.y, q.z, q.w)};
    }
    Matrix WorldMatrix(const Pose& pose)
    {
        Matrix result;
        const auto q = pose.rotation;
        DirectX::XMStoreFloat4x4(
            &result,
            DirectX::XMMatrixScaling(pose.scale[0], pose.scale[1], pose.scale[2]) *
                DirectX::XMMatrixRotationQuaternion(DirectX::XMVectorSet(q.GetX(), q.GetY(), q.GetZ(), q.GetW())) *
                DirectX::XMMatrixTranslation(pose.position.GetX(), pose.position.GetY(), pose.position.GetZ()));
        return result;
    }
    JPH::RefConst<JPH::Shape> Checked(const JPH::Shape::ShapeResult& result)
    {
        if (result.HasError())
            throw std::runtime_error(result.GetError().c_str());
        return result.Get();
    }
    std::vector<Engine::MeshData> ColliderMeshes(const PhysicsObject& object, const std::filesystem::path& root)
    {
        const auto& c = *object.boxCollider;
        if (c.shape == "terrain")
        {
            if (!object.terrain)
                throw std::runtime_error("Terrain collider needs a Terrain");
            return {GenreGeometry::Terrain(*object.terrain)};
        }
        if (c.shape == "tilemap")
        {
            if (!object.tilemap)
                throw std::runtime_error("Tile collider needs a Tilemap");
            return {GenreGeometry::TileCollision(*object.tilemap)};
        }
        const auto model = c.model.empty() ? object.Model() : c.model;
        if (model.empty())
            throw std::runtime_error("Mesh collider requires a model");
        std::string error;
        const auto meshes = Engine::ModelAssets::Load(root / model, error);
        if (!meshes)
            throw std::runtime_error(error);
        return *meshes;
    }
    JPH::RefConst<JPH::Shape> Shape(const PhysicsObject& object, const Pose& pose, const std::filesystem::path& root)
    {
        const auto& c = *object.boxCollider;
        JPH::RefConst<JPH::Shape> shape;
        const auto scale = V(pose.scale).Abs();
        if (c.shape == "box")
            shape = Checked(JPH::BoxShapeSettings(V(c.size) * scale * 0.5f, 0).Create());
        else if (c.shape == "sphere")
            shape = Checked(JPH::SphereShapeSettings(c.radius * scale.ReduceMax()).Create());
        else if (c.shape == "capsule")
            shape = c.halfHeight == 0
                        ? Checked(JPH::SphereShapeSettings(c.radius * scale.ReduceMax()).Create())
                        : Checked(JPH::CapsuleShapeSettings(c.halfHeight * scale.GetY(),
                                                            c.radius * std::max(scale.GetX(), scale.GetZ()))
                                      .Create());
        else
        {
            const auto meshes = ColliderMeshes(object, root);
            JPH::TriangleList triangles;
            JPH::Array<JPH::Vec3> points;
            for (const auto& mesh : meshes)
                for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
                {
                    const auto vertex = [&](size_t index)
                    {
                        return V(mesh.vertices.at(mesh.indices.at(index)).position) * V(pose.scale);
                    };
                    auto a = vertex(i), b = vertex(i + 1), d = vertex(i + 2);
                    const auto& va = mesh.vertices.at(mesh.indices.at(i));
                    const auto& vb = mesh.vertices.at(mesh.indices.at(i + 1));
                    const auto& vd = mesh.vertices.at(mesh.indices.at(i + 2));
                    const auto facing =
                        (V(vb.position) - V(va.position)).Cross(V(vd.position) - V(va.position)).Dot(V(va.normal));
                    if ((facing < 0) != (pose.scale[0] * pose.scale[1] * pose.scale[2] < 0))
                        std::swap(b, d);
                    triangles.emplace_back(a, b, d);
                    points.push_back(a);
                    points.push_back(b);
                    points.push_back(d);
                }
            if (!c.convex && object.rigidBody && object.rigidBody->enabled && object.rigidBody->motion == "dynamic")
                throw std::runtime_error("Dynamic mesh colliders require Convex");
            shape = c.convex ? Checked(JPH::ConvexHullShapeSettings(points).Create())
                             : Checked(JPH::MeshShapeSettings(triangles).Create());
        }
        return Checked(
            JPH::RotatedTranslatedShapeSettings(V(c.center) * V(pose.scale), JPH::Quat::sIdentity(), shape).Create());
    }
    bool PhysicsController(const PhysicsObject& object)
    {
        return object.playerController && object.playerController->enabled && object.playerController->usePhysics;
    }
    Pose ColliderPose(const PhysicsObject& object, const Matrix& matrix, bool& baked)
    {
        try
        {
            baked = false;
            return ReadPose(matrix);
        }
        catch (const std::exception&)
        {
            if ((object.rigidBody && object.rigidBody->enabled) || (PhysicsController(object)))
                throw;
            baked = true;
            Pose pose;
            pose.position = {matrix._41, matrix._42, matrix._43};
            return pose;
        }
    }
    template <class Transform>
    bool BakePrimitivePoints(const BoxColliderComponent& c, const Transform& transform, JPH::Array<JPH::Vec3>& points)
    {
        if (c.shape == "box")
            for (int corner = 0; corner < 8; ++corner)
                points.push_back(
                    transform({c.size[0] * ((corner & 1) ? 0.5f : -0.5f), c.size[1] * ((corner & 2) ? 0.5f : -0.5f),
                               c.size[2] * ((corner & 4) ? 0.5f : -0.5f)}));
        else if (c.shape == "sphere" || c.shape == "capsule")
            for (int ring = 0; ring <= 16; ++ring)
                for (int side = 0; side < 32; ++side)
                {
                    const float latitude = JPH::JPH_PI * (static_cast<float>(ring) / 16 - 0.5f),
                                longitude = 2 * JPH::JPH_PI * side / 32;
                    float y = std::sin(latitude) * c.radius;
                    if (c.shape == "capsule")
                        y += ring < 8 ? -c.halfHeight : c.halfHeight;
                    points.push_back(transform({std::cos(latitude) * std::cos(longitude) * c.radius, y,
                                                std::cos(latitude) * std::sin(longitude) * c.radius}));
                }
        else
            return false;
        return true;
    }
    JPH::RefConst<JPH::Shape> BakedShape(const PhysicsObject& object, const Matrix& matrix,
                                         const std::filesystem::path& root)
    {
        const auto& c = *object.boxCollider;
        const auto transform = [&](std::array<float, 3> point)
        {
            for (size_t i = 0; i < 3; ++i)
                point[i] += c.center[i];
            DirectX::XMFLOAT3 output;
            DirectX::XMStoreFloat3(
                &output, DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(point[0], point[1], point[2], 0),
                                                           DirectX::XMLoadFloat4x4(&matrix)));
            return JPH::Vec3(output.x, output.y, output.z);
        };
        JPH::Array<JPH::Vec3> points;
        if (!BakePrimitivePoints(c, transform, points))
        {
            const auto meshes = ColliderMeshes(object, root);
            JPH::TriangleList triangles;
            for (const auto& mesh : meshes)
                for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
                {
                    const auto& a = mesh.vertices.at(mesh.indices.at(i));
                    const auto& b = mesh.vertices.at(mesh.indices.at(i + 1));
                    const auto& d = mesh.vertices.at(mesh.indices.at(i + 2));
                    auto p = transform(a.position), q = transform(b.position), r = transform(d.position);
                    const auto facing =
                        (V(b.position) - V(a.position)).Cross(V(d.position) - V(a.position)).Dot(V(a.normal));
                    const bool mirrored =
                        DirectX::XMVectorGetX(DirectX::XMMatrixDeterminant(DirectX::XMLoadFloat4x4(&matrix))) < 0;
                    if ((facing < 0) != mirrored)
                        std::swap(q, r);
                    triangles.emplace_back(p, q, r);
                    points.push_back(p);
                    points.push_back(q);
                    points.push_back(r);
                }
            if (!c.convex)
                return Checked(JPH::MeshShapeSettings(triangles).Create());
        }
        return Checked(JPH::ConvexHullShapeSettings(points, 0).Create());
    }
    template <class Component> bool Enabled(const Component& component)
    {
        return component && component->enabled;
    }
    struct TransformBackup
    {
        size_t index;
        std::array<float, 3> position, rotation, scale;
    };

    void ValidateControllers(const PhysicsFrame& layout)
    {
        for (const auto& object : layout.objects)
            if (object.playerController)
            {
                const auto& controller = *object.playerController;
                const auto bounded = [](float value, float limit)
                {
                    return std::isfinite(value) && value >= 0 && value <= limit;
                };
                if (!bounded(controller.moveSpeed, 1000) || !bounded(controller.gravity, 1000) ||
                    !bounded(controller.jumpSpeed, 1000) || !bounded(controller.stepHeight, 10) ||
                    !bounded(controller.maxSlopeDegrees, 89))
                    throw std::runtime_error("Invalid PlayerController movement settings");
            }
    }

    std::vector<TransformBackup> CaptureTransforms(const PhysicsFrame& layout)
    {
        std::vector<TransformBackup> transforms;
        transforms.reserve(layout.objects.size());
        for (size_t i = 0; i < layout.objects.size(); ++i)
        {
            const auto& object = layout.objects[i];
            if ((PhysicsController(object)) || (object.rigidBody && object.rigidBody->enabled))
                transforms.push_back({i, object.position, object.rotation, object.scale});
        }
        return transforms;
    }

    void RestoreTransforms(PhysicsFrame& layout, const std::vector<TransformBackup>& transforms)
    {
        for (const auto& transform : transforms)
        {
            auto& object = layout.objects[transform.index];
            object.position = transform.position;
            object.rotation = transform.rotation;
            object.scale = transform.scale;
        }
    }

    size_t ObjectDepth(const PhysicsFrame& before, size_t index)
    {
        size_t count = 0;
        auto parent = before.objects.at(index).parentId;
        while (!parent.empty())
        {
            const auto found = std::find_if(before.objects.begin(), before.objects.end(),
                                            [&](const auto& object)
                                            {
                                                return object.id == parent;
                                            });
            if (found == before.objects.end())
                break;
            parent = found->parentId;
            ++count;
        }
        return count;
    }
    using Contact = std::tuple<std::string, std::string, bool>;
}

namespace SceneRuntime
{
    struct PhysicsWorld::Impl final : JPH::ContactListener
    {
        struct Entry
        {
            size_t index = 0;
            JPH::BodyID body;
            JPH::Ref<JPH::CharacterVirtual> character;
            Pose pose;
            bool baked = false;
            Matrix basis{};
        };
        Layers layers;
        BroadFilter broadFilter;
        PairFilter pairFilter;
        JPH::TempAllocatorImpl temporary{16 * 1024 * 1024};
        JPH::JobSystemSingleThreaded jobs{JPH::cMaxPhysicsJobs};
        JPH::PhysicsSystem system;
        PhysicsFrame definition;
        std::map<std::string, Entry> entries;
        std::map<JPH::uint32, size_t> bodyObjects;
        std::vector<JPH::Ref<JPH::Constraint>> constraints;
        std::set<Contact> contacts;
        explicit Impl(const PhysicsFrame& layout) : definition(layout)
        {
            system.Init(8192, 0, 32768, 32768, layers, broadFilter, pairFilter);
            system.SetGravity({layout.settings.gravity[0], layout.settings.gravity[1], layout.settings.gravity[2]});
            system.SetContactListener(this);
        }
        ~Impl()
        {
            for (const auto& constraint : constraints)
                system.RemoveConstraint(constraint);
            constraints.clear();
            for (auto& [id, entry] : entries)
            {
                static_cast<void>(id);
                if (entry.character)
                    entry.character = nullptr;
                else if (!entry.body.IsInvalid())
                {
                    system.GetBodyInterface().RemoveBody(entry.body);
                    system.GetBodyInterface().DestroyBody(entry.body);
                }
            }
        }
        bool Allowed(size_t a, size_t b) const
        {
            const auto disconnected = [&](size_t x, size_t y)
            {
                const auto& object = definition.objects[x];
                return object.joint && object.joint->enabled && !object.joint->collideConnected &&
                       object.joint->target == definition.objects[y].id;
            };
            if (disconnected(a, b) || disconnected(b, a))
                return false;
            const auto& x = *definition.objects[a].boxCollider;
            const auto& y = *definition.objects[b].boxCollider;
            return (x.mask & (1u << y.layer)) && (y.mask & (1u << x.layer)) && !Related(definition, a, b);
        }
        JPH::ValidateResult OnContactValidate(const JPH::Body& a, const JPH::Body& b, JPH::RVec3Arg,
                                              const JPH::CollideShapeResult&) override
        {
            return Allowed(static_cast<size_t>(a.GetUserData()), static_cast<size_t>(b.GetUserData()))
                       ? JPH::ValidateResult::AcceptAllContactsForThisBodyPair
                       : JPH::ValidateResult::RejectAllContactsForThisBodyPair;
        }
        struct Filter final : JPH::BodyFilter
        {
            const Impl& world;
            size_t actor;
            unsigned int mask;
            bool sensors;
            std::string ignore;
            Filter(const Impl& w, size_t a, unsigned int m, bool s, std::string id = {})
                : world(w), actor(a), mask(m), sensors(s), ignore(std::move(id))
            {
            }
            bool ShouldCollide(const JPH::BodyID& body) const override
            {
                const auto found = world.bodyObjects.find(body.GetIndexAndSequenceNumber());
                if (found == world.bodyObjects.end())
                    return false;
                const auto& object = world.definition.objects[found->second];
                const auto& c = *object.boxCollider;
                return object.id != ignore && (mask & (1u << c.layer)) && (sensors || !c.isTrigger) &&
                       (actor == SIZE_MAX || world.Allowed(actor, found->second));
            }
        };
        void CreateCharacter(Entry& entry, const PhysicsObject& object, const JPH::RefConst<JPH::Shape>& shape,
                             const ScenePhysics::States& states, bool body)
        {
            if (body || object.boxCollider->isTrigger ||
                (object.boxCollider->shape == "mesh" && !object.boxCollider->convex))
                throw std::runtime_error("Physics player requires a solid convex collider and no RigidBody");
            JPH::CharacterVirtualSettings settings;
            settings.mShape = shape;
            settings.mInnerBodyShape = shape;
            settings.mInnerBodyLayer = 1;
            settings.mMaxSlopeAngle = object.playerController->maxSlopeDegrees * JPH::JPH_PI / 180;
            settings.mCharacterPadding = 0.001f;
            settings.mPredictiveContactDistance = 0.02f;
            entry.character =
                new JPH::CharacterVirtual(&settings, entry.pose.position, entry.pose.rotation, entry.index, &system);
            entry.body = entry.character->GetInnerBodyID();
            const auto state = states.find(object.id);
            if (state != states.end())
                entry.character->SetLinearVelocity({0, state->second.verticalSpeed, 0});
        }

        void CreateBody(Entry& entry, const PhysicsObject& object, const JPH::RefConst<JPH::Shape>& shape,
                        const Impl* old, bool controller, bool body)
        {
            const auto motion =
                body ? (object.rigidBody->motion == "dynamic" ? JPH::EMotionType::Dynamic : JPH::EMotionType::Kinematic)
                     : (controller ? JPH::EMotionType::Kinematic : JPH::EMotionType::Static);
            JPH::BodyCreationSettings settings(shape, entry.pose.position, entry.pose.rotation, motion,
                                               motion == JPH::EMotionType::Static ? 0 : 1);
            settings.mUserData = entry.index;
            settings.mIsSensor = object.boxCollider->isTrigger;
            settings.mCollideKinematicVsNonDynamic = true;
            if (body)
            {
                const auto& rb = *object.rigidBody;
                if (rb.planar)
                    settings.mAllowedDOFs = JPH::EAllowedDOFs::Plane2D;
                settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
                settings.mMassPropertiesOverride.mMass = rb.mass;
                settings.mFriction = rb.friction;
                settings.mRestitution = rb.restitution;
                settings.mGravityFactor = rb.gravityScale;
                settings.mLinearDamping = rb.linearDamping;
                settings.mAngularDamping = rb.angularDamping;
                settings.mMaxLinearVelocity = 100000;
                settings.mMaxAngularVelocity = 100000;
                settings.mLinearVelocity = V(rb.velocity);
                settings.mAngularVelocity = V(rb.angularVelocity);
                settings.mMotionQuality =
                    rb.continuous ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
                if (old)
                {
                    const auto found = old->entries.find(object.id);
                    if (found != old->entries.end() &&
                        old->definition.objects[found->second.index].bodyInstance == object.bodyInstance &&
                        old->definition.objects[found->second.index].entity == object.entity &&
                        old->definition.objects[found->second.index].rigidBody == object.rigidBody)
                    {
                        settings.mLinearVelocity = old->system.GetBodyInterface().GetLinearVelocity(found->second.body);
                        settings.mAngularVelocity =
                            old->system.GetBodyInterface().GetAngularVelocity(found->second.body);
                    }
                }
            }
            entry.body = system.GetBodyInterface().CreateAndAddBody(settings, JPH::EActivation::Activate);
            if (entry.body.IsInvalid())
                throw std::runtime_error("Physics body creation failed");
        }

        void BuildEntry(size_t i, const std::vector<Matrix>& matrices, const std::filesystem::path& root,
                        const Impl* old, const ScenePhysics::States& states)
        {
            const auto& object = definition.objects[i];
            if (!object.boxCollider || !object.boxCollider->enabled)
            {
                if ((object.rigidBody && object.rigidBody->enabled) || (PhysicsController(object)))
                    throw std::runtime_error("Rigid body or physics player requires an enabled collider: " + object.id);
                return;
            }
            Entry entry;
            entry.index = i;
            entry.pose = ColliderPose(object, matrices[i], entry.baked);
            entry.basis = matrices[i];
            const auto shape = entry.baked ? BakedShape(object, matrices[i], root) : Shape(object, entry.pose, root);
            const bool controller = Enabled(object.playerController);
            const bool body = Enabled(object.rigidBody);
            if (controller && object.playerController->usePhysics)
            {
                CreateCharacter(entry, object, shape, states, body);
            }
            else
            {
                CreateBody(entry, object, shape, old, controller, body);
            }
            bodyObjects[entry.body.GetIndexAndSequenceNumber()] = i;
            entries.emplace(object.id, std::move(entry));
        }
        void Build(const std::vector<Matrix>& matrices, const std::filesystem::path& root, const Impl* old,
                   const ScenePhysics::States& states)
        {
            if (std::count_if(definition.objects.begin(), definition.objects.end(),
                              [](const auto& object)
                              {
                                  return object.boxCollider && object.boxCollider->enabled;
                              }) > 8192)
                throw std::runtime_error("Physics body limit exceeded");
            const auto inputs = QueryGeometryFiles(definition, root);
            for (size_t i = 0; i < definition.objects.size(); ++i)
            {
                BuildEntry(i, matrices, root, old, states);
            }
            if (!QueryFilesMatch(inputs))
                throw std::runtime_error("Collider model changed during import");
            system.OptimizeBroadPhase();
            BuildJoints(matrices);
            if (old)
                contacts = old->contacts;
        }
        using EntryIterator = std::map<std::string, Entry>::const_iterator;
        void ValidateJointBodies(const PhysicsObject& object, EntryIterator owner, EntryIterator target) const
        {
            const auto& joint = *object.joint;
            if (owner == entries.end() || owner->second.character || !object.rigidBody || !object.rigidBody->enabled ||
                object.rigidBody->motion != "dynamic")
                throw std::runtime_error("Joint owner requires a dynamic RigidBody: " + object.id);
            if (!joint.target.empty() && (target == entries.end() || target == owner || target->second.character))
                throw std::runtime_error("Joint target requires a different collider body: " + joint.target);
        }
        void BuildJoints(const std::vector<Matrix>& matrices)
        {
            const auto transform = [](const std::array<float, 3>& value, const Matrix& matrix, bool direction)
            {
                const auto vector = DirectX::XMVectorSet(value[0], value[1], value[2], direction ? 0.0f : 1.0f);
                const auto world =
                    direction ? DirectX::XMVector3Normalize(
                                    DirectX::XMVector3TransformNormal(vector, DirectX::XMLoadFloat4x4(&matrix)))
                              : DirectX::XMVector3TransformCoord(vector, DirectX::XMLoadFloat4x4(&matrix));
                return JPH::Vec3{DirectX::XMVectorGetX(world), DirectX::XMVectorGetY(world),
                                 DirectX::XMVectorGetZ(world)};
            };
            for (size_t index = 0; index < definition.objects.size(); ++index)
            {
                const auto& object = definition.objects[index];
                if (!object.joint || !object.joint->enabled)
                    continue;
                const auto& joint = *object.joint;
                joint.Validate();
                const auto owner = entries.find(object.id), target = entries.find(joint.target);
                ValidateJointBodies(object, owner, target);
                const JPH::BodyID ids[]{owner->second.body,
                                        target == entries.end() ? JPH::BodyID{} : target->second.body};
                JPH::BodyLockMultiWrite lock(system.GetBodyLockInterface(), ids, 2);
                auto* first = lock.GetBody(0);
                auto* second = target == entries.end() ? &JPH::Body::sFixedToWorld : lock.GetBody(1);
                if (!first || !second)
                    throw std::runtime_error("Cannot lock Joint bodies");
                const auto point = transform(joint.anchor, matrices[index], false);
                const auto connected = target == entries.end()
                                           ? V(joint.connectedAnchor)
                                           : transform(joint.connectedAnchor, matrices[target->second.index], false);
                JPH::Ref<JPH::Constraint> constraint = PhysicsJoints::Create(
                    joint, *first, *second, point, connected, transform(joint.axis, matrices[index], true));
                lock.ReleaseLocks();
                system.AddConstraint(constraint);
                constraints.push_back(std::move(constraint));
            }
        }
        static bool MatchesController(const PhysicsValue<PlayerControllerComponent>& a,
                                      const PhysicsValue<PlayerControllerComponent>& b)
        {
            if (a.HasValue() != b.HasValue())
                return false;
            if (!a)
                return true;
            // 移動設定は Step が使用します。ワールドを無効化するのは、物理ボディーの構築設定の変更だけです。
            return a->id == b->id && a->enabled == b->enabled && a->usePhysics == b->usePhysics &&
                   a->maxSlopeDegrees == b->maxSlopeDegrees;
        }
        static bool MatchesDefinition(const PhysicsObject& a, const PhysicsObject& b)
        {
            if (a.boxCollider && a.boxCollider->shape == "mesh" && a.Model() != b.Model())
                return false;
            return !(a.entity != b.entity || a.colliderInstance != b.colliderInstance ||
                     a.bodyInstance != b.bodyInstance || a.controllerInstance != b.controllerInstance ||
                     a.jointInstance != b.jointInstance || a.id != b.id || a.parentId != b.parentId ||
                     a.boxCollider != b.boxCollider || a.rigidBody != b.rigidBody ||
                     !MatchesController(a.playerController, b.playerController) || a.terrain != b.terrain ||
                     a.tilemap != b.tilemap || a.joint != b.joint);
        }
        bool MatchesShape(const PhysicsObject& a, const Matrix& matrix) const
        {
            const auto& entry = entries.at(a.id);
            // 変更されない構築時の行列は、すでに形状の検証を通過しています。
            if (std::memcmp(&matrix, &entry.basis, sizeof(Matrix)) == 0)
                return true;
            bool baked = false;
            const auto scale = ColliderPose(a, matrix, baked).scale;
            const auto& stored = entry.pose.scale;
            if (baked != entry.baked)
                return false;
            if (baked)
                for (size_t row = 0; row < 3; ++row)
                    for (size_t column = 0; column < 3; ++column)
                        if (matrix.m[row][column] != entry.basis.m[row][column])
                            return false;
            for (size_t axis = 0; axis < 3; ++axis)
                if (std::abs(scale[axis] - stored[axis]) > 1e-5f * std::max(1.0f, std::abs(stored[axis])))
                    return false;
            return true;
        }
        bool Matches(const PhysicsFrame& layout, const std::vector<Matrix>& matrices) const
        {
            if (layout.settings.gravity != definition.settings.gravity)
                return false;
            if (layout.objects.size() != definition.objects.size())
                return false;
            for (size_t i = 0; i < layout.objects.size(); ++i)
            {
                const auto& a = layout.objects[i];
                const auto& b = definition.objects[i];
                if (!MatchesDefinition(a, b))
                    return false;
                if (a.boxCollider && a.boxCollider->enabled)
                {
                    if (!MatchesShape(a, matrices[i]))
                        return false;
                }
            }
            return true;
        }
        void SynchronizeQueries(const PhysicsFrame& layout, const std::vector<Matrix>& matrices)
        {
            for (auto& [id, entry] : entries)
            {
                static_cast<void>(id);
                const auto& matrix = matrices.at(entry.index);
                if (std::memcmp(&matrix, &entry.basis, sizeof(Matrix)) == 0)
                    continue;
                bool baked = false;
                const auto pose = ColliderPose(layout.objects.at(entry.index), matrix, baked);
                if (entry.character)
                {
                    entry.character->SetPosition(pose.position);
                    entry.character->SetRotation(pose.rotation);
                }
                else
                    system.GetBodyInterface().SetPositionAndRotation(entry.body, pose.position, pose.rotation,
                                                                     JPH::EActivation::DontActivate);
                entry.pose = pose;
                entry.basis = matrix;
            }
        }
        void Gather(std::vector<PhysicsContactEvent>& events)
        {
            std::set<Contact> current;
            for (const auto& [id, entry] : entries)
            {
                const auto& object = definition.objects[entry.index];
                if (!entry.character && system.GetBodyInterface().GetMotionType(entry.body) == JPH::EMotionType::Static)
                    continue;
                JPH::RefConst<JPH::Shape> shape;
                auto transform = JPH::RMat44::sIdentity();
                {
                    JPH::BodyLockRead lock(system.GetBodyLockInterface(), entry.body);
                    if (!lock.Succeeded())
                        continue;
                    shape = lock.GetBody().GetShape();
                    transform = lock.GetBody().GetCenterOfMassTransform();
                }
                JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
                JPH::CollideShapeSettings settings;
                settings.mMaxSeparationDistance = 0.003f;
                system.GetNarrowPhaseQuery().CollideShape(shape, JPH::Vec3::sOne(), transform, settings,
                                                          JPH::RVec3::sZero(), collector, {}, {},
                                                          Filter(*this, entry.index, 0xffffffffu, true));
                for (const auto& hit : collector.mHits)
                {
                    const auto other = bodyObjects.find(hit.mBodyID2.GetIndexAndSequenceNumber());
                    if (other == bodyObjects.end())
                        continue;
                    const auto& b = definition.objects[other->second];
                    const bool trigger = object.boxCollider->isTrigger || b.boxCollider->isTrigger;
                    current.emplace(std::min(id, b.id), std::max(id, b.id), trigger);
                }
            }
            for (const auto& pair : current)
                if (!contacts.contains(pair))
                    events.push_back({std::get<0>(pair), std::get<1>(pair), "Enter", std::get<2>(pair)});
            for (const auto& pair : contacts)
                if (!current.contains(pair))
                    events.push_back({std::get<0>(pair), std::get<1>(pair), "Exit", std::get<2>(pair)});
            contacts = std::move(current);
        }
        bool ConstrainActor(const PhysicsFrame& candidate, PhysicsFrame& resolved, const std::vector<Matrix>& start,
                            std::vector<Matrix>& desired, size_t index, const std::filesystem::path& root,
                            std::string& error)
        {
            const auto& object = candidate.objects.at(index);
            if (object.boxCollider->shape == "mesh" && !object.boxCollider->convex)
                throw std::runtime_error("Root motion requires a convex moving collider");
            if (!PhysicsTransforms::Resolve(resolved, desired, error))
                return false;
            const auto from = ReadPose(start.at(index)), to = ReadPose(desired.at(index));
            const auto shape = Shape(object, from, root);
            JPH::CharacterVirtualSettings settings;
            settings.mShape = shape;
            settings.mCharacterPadding = 0.001f;
            settings.mPredictiveContactDistance = 0.02f;
            JPH::CharacterVirtual character(&settings, from.position, from.rotation, index, &system);
            const auto velocity = (to.position - from.position) / 0.1f;
            JPH::CharacterVirtual::ExtendedUpdateSettings update;
            update.mWalkStairsStepUp = JPH::Vec3::sZero();
            update.mStickToFloorStepDown = JPH::Vec3::sZero();
            for (int step = 0; step < 12; ++step)
            {
                DirectX::XMFLOAT4 rotation;
                DirectX::XMStoreFloat4(&rotation, DirectX::XMQuaternionSlerp(
                                                      DirectX::XMVectorSet(from.rotation.GetX(), from.rotation.GetY(),
                                                                           from.rotation.GetZ(), from.rotation.GetW()),
                                                      DirectX::XMVectorSet(to.rotation.GetX(), to.rotation.GetY(),
                                                                           to.rotation.GetZ(), to.rotation.GetW()),
                                                      float(step + 1) / 12));
                character.SetRotation({rotation.x, rotation.y, rotation.z, rotation.w});
                character.SetLinearVelocity(velocity);
                character.ExtendedUpdate(0.1f / 12, JPH::Vec3::sZero(), update,
                                         system.GetDefaultBroadPhaseLayerFilter(1), system.GetDefaultLayerFilter(1),
                                         Filter(*this, index, 0xffffffffu, false, object.id), {}, temporary);
            }
            auto actual = to;
            actual.position = JPH::Vec3(character.GetPosition());
            Matrix local = WorldMatrix(actual);
            if (!object.parentId.empty())
            {
                std::vector<Matrix> parents;
                if (!PhysicsTransforms::Resolve(resolved, parents, error))
                    return false;
                const auto parent = std::find_if(resolved.objects.begin(), resolved.objects.end(),
                                                 [&](const auto& item)
                                                 {
                                                     return item.id == object.parentId;
                                                 });
                if (parent == resolved.objects.end() ||
                    !PhysicsTransforms::WorldToLocal(local, parents[size_t(parent - resolved.objects.begin())], local))
                    throw std::runtime_error("Root collision parent conversion failed");
            }
            PhysicsObject placement;
            if (!PhysicsTransforms::ReadTransform(local, object, placement))
                throw std::runtime_error("Root collision transform cannot be represented");
            auto& result = resolved.objects[index];
            result.position = placement.position;
            result.rotation = placement.rotation;
            result.scale = placement.scale;
            if (const auto entry = entries.find(object.id); entry != entries.end())
            {
                if (entry->second.character)
                {
                    entry->second.character->SetPosition(actual.position);
                    entry->second.character->SetRotation(actual.rotation);
                }
                else
                    system.GetBodyInterface().SetPositionAndRotation(entry->second.body, actual.position,
                                                                     actual.rotation, JPH::EActivation::Activate);
                entry->second.pose.position = actual.position;
                entry->second.pose.rotation = actual.rotation;
                entry->second.basis = WorldMatrix(entry->second.pose);
            }
            return true;
        }
        struct AdvanceBackup
        {
            JPH::StateRecorderImpl snapshot;
            std::map<std::string, JPH::StateRecorderImpl> characters;
            std::set<Contact> previousContacts;
            std::map<std::string, Pose> previousPoses;
        };
        void SaveAdvance(AdvanceBackup& backup)
        {
            Engine::CpuScope snapshotScope("Physics rollback snapshot");
            system.SaveState(backup.snapshot);
            backup.previousContacts = contacts;
            for (const auto& [id, entry] : entries)
            {
                backup.previousPoses[id] = entry.pose;
                if (entry.character)
                    entry.character->SaveState(backup.characters[id]);
            }
        }
        void RestoreAdvance(AdvanceBackup& backup)
        {
            backup.snapshot.Rewind();
            system.RestoreState(backup.snapshot);
            contacts = std::move(backup.previousContacts);
            for (auto& [id, entry] : entries)
            {
                entry.pose = backup.previousPoses.at(id);
                if (entry.character)
                {
                    backup.characters.at(id).Rewind();
                    entry.character->RestoreState(backup.characters.at(id));
                }
            }
        }
        struct StepInput
        {
            float seconds, horizontal, vertical;
            bool jump;
        };
        struct CharacterStepInput
        {
            float horizontal, vertical, magnitude, tick;
            int step;
            bool jump;
        };
        static void AdvanceKinematicPose(const PhysicsObject& object, float seconds, Pose& pose)
        {
            const auto& body = object.rigidBody;
            if (body && body->enabled)
            {
                pose.position += V(body->velocity) * seconds;
                const auto angular = V(body->angularVelocity);
                if (angular.LengthSq() > 1e-12f)
                    pose.rotation =
                        JPH::Quat::sRotation(angular.Normalized(), angular.Length() * seconds) * pose.rotation;
            }
        }
        void SynchronizeBody(Entry& entry, const Matrix& matrix, const PhysicsObject& object, float seconds)
        {
            auto& bodies = system.GetBodyInterface();
            const bool moved = entry.baked ? !JPH::Vec3(matrix._41, matrix._42, matrix._43).IsClose(entry.pose.position)
                                           : !PhysicsTransforms::Matches(WorldMatrix(entry.pose), matrix);
            auto pose = entry.pose;
            const bool kinematic = !entry.character && bodies.GetMotionType(entry.body) == JPH::EMotionType::Kinematic;
            if (moved || kinematic)
            {
                bool baked = false;
                pose = ColliderPose(object, matrix, baked);
            }
            if (entry.character)
            {
                if (moved)
                {
                    entry.character->SetPosition(pose.position);
                    entry.character->SetRotation(pose.rotation);
                }
            }
            else if (kinematic)
            {
                AdvanceKinematicPose(object, seconds, pose);
                bodies.MoveKinematic(entry.body, pose.position, pose.rotation, seconds);
            }
            else if (moved)
                bodies.SetPositionAndRotation(entry.body, pose.position, pose.rotation, JPH::EActivation::Activate);
        }
        void SynchronizeBodies(const PhysicsFrame& layout, const std::vector<Matrix>& matrices, float seconds,
                               const std::map<std::string, std::array<float, 3>>& impulses)
        {
            auto& bodies = system.GetBodyInterface();
            for (auto& [id, entry] : entries)
            {
                SynchronizeBody(entry, matrices[entry.index], layout.objects[entry.index], seconds);
                const auto impulse = impulses.find(id);
                if (impulse != impulses.end() && bodies.GetMotionType(entry.body) == JPH::EMotionType::Dynamic)
                    bodies.AddImpulse(entry.body, V(impulse->second));
            }
        }

        static DirectX::XMMATRIX ControllerParent(const PhysicsFrame& layout, const std::vector<Matrix>& matrices,
                                                  const PhysicsObject& object)
        {
            auto parent = DirectX::XMMatrixIdentity();
            if (!object.parentId.empty())
            {
                const auto found = std::find_if(layout.objects.begin(), layout.objects.end(),
                                                [&](const auto& item)
                                                {
                                                    return item.id == object.parentId;
                                                });
                parent = DirectX::XMLoadFloat4x4(&matrices[static_cast<size_t>(found - layout.objects.begin())]);
            }
            return parent;
        }
        static float ControllerSpeed(const PhysicsObject& object, const PlayerControllerComponent& controller)
        {
            return object.animator && object.animator->enabled && object.animator->rootMotion ? 0.0f
                                                                                              : controller.moveSpeed;
        }
        void UpdateCharacter(const PhysicsFrame& layout, ScenePhysics::States& states,
                             const std::vector<Matrix>& matrices, const std::string& id, Entry& entry,
                             const CharacterStepInput& input)
        {
            const auto& [horizontal, vertical, magnitude, tick, step, jump] = input;
            const auto& object = layout.objects[entry.index];
            const auto& controller = *object.playerController;
            auto& character = *entry.character;
            character.UpdateGroundVelocity();
            auto& state = states[id];
            state.grounded = character.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
            const auto ground = state.grounded ? character.GetGroundVelocity() : JPH::Vec3::sZero();
            float vy = character.GetLinearVelocity().GetY();
            if (state.grounded && vy <= ground.GetY() + 0.1f)
                vy = ground.GetY();
            if (step == 0 && jump && state.grounded && controller.useGravity)
            {
                vy = controller.jumpSpeed + ground.GetY();
                state.grounded = false;
            }
            if (controller.useGravity)
                vy -= controller.gravity * tick;
            else
                vy = 0;
            const auto parent = ControllerParent(layout, matrices, object);
            DirectX::XMFLOAT3 movement;
            const float inputSpeed = ControllerSpeed(object, controller);
            DirectX::XMStoreFloat3(&movement, DirectX::XMVector3TransformNormal(
                                                  DirectX::XMVectorSet(horizontal / magnitude * inputSpeed, 0,
                                                                       vertical / magnitude * inputSpeed, 0),
                                                  parent));
            character.SetLinearVelocity({movement.x + ground.GetX(), vy + movement.y, movement.z + ground.GetZ()});
            JPH::CharacterVirtual::ExtendedUpdateSettings settings;
            settings.mWalkStairsStepUp = {0, controller.stepHeight, 0};
            settings.mStickToFloorStepDown = {0, controller.useGravity ? -0.05f : 0, 0};
            character.ExtendedUpdate(tick, {0, controller.useGravity ? -controller.gravity : 0, 0}, settings,
                                     system.GetDefaultBroadPhaseLayerFilter(1), system.GetDefaultLayerFilter(1),
                                     Filter(*this, entry.index, 0xffffffffu, false), {}, temporary);
            state.verticalSpeed = character.GetLinearVelocity().GetY();
            state.grounded = character.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
        }

        std::map<std::string, Matrix> PhysicalMatrices(const PhysicsFrame& layout)
        {
            auto& bodies = system.GetBodyInterface();
            std::map<std::string, Matrix> physical;
            for (auto& [id, entry] : entries)
            {
                entry.pose.position = entry.character ? JPH::Vec3(entry.character->GetPosition())
                                                      : JPH::Vec3(bodies.GetPosition(entry.body));
                entry.pose.rotation = entry.character ? entry.character->GetRotation() : bodies.GetRotation(entry.body);
                const auto& rb = layout.objects[entry.index].rigidBody;
                if (entry.character || (rb && rb->enabled))
                    physical[id] = WorldMatrix(entry.pose);
            }
            return physical;
        }
        void ApplyPhysicsTransforms(PhysicsFrame& layout)
        {
            const auto physical = PhysicalMatrices(layout);
            std::map<std::string, Matrix> resolved;
            const auto world = [&](auto&& self, const std::string& id) -> Matrix
            {
                if (resolved.contains(id))
                    return resolved.at(id);
                if (physical.contains(id))
                    return resolved[id] = physical.at(id);
                const auto found = std::find_if(layout.objects.begin(), layout.objects.end(),
                                                [&](const auto& item)
                                                {
                                                    return item.id == id;
                                                });
                Matrix local{};
                if (found == layout.objects.end() || !PhysicsTransforms::Compose(*found, local))
                    throw std::runtime_error("Invalid physics parent transform");
                if (!found->parentId.empty())
                {
                    const auto p = self(self, found->parentId);
                    DirectX::XMStoreFloat4x4(&local, DirectX::XMLoadFloat4x4(&local) * DirectX::XMLoadFloat4x4(&p));
                }
                return resolved[id] = local;
            };
            for (auto& object : layout.objects)
                if (physical.contains(object.id))
                {
                    Matrix local = physical.at(object.id);
                    if (!object.parentId.empty() &&
                        !PhysicsTransforms::WorldToLocal(local, world(world, object.parentId), local))
                        throw std::runtime_error("Physics parent conversion failed");
                    PhysicsObject placement;
                    if (!PhysicsTransforms::ReadTransform(local, object, placement))
                        throw std::runtime_error("Physics cannot save inherited shear");
                    object.position = placement.position;
                    object.rotation = placement.rotation;
                    object.scale = placement.scale;
                }
        }
        void Step(PhysicsFrame& layout, ScenePhysics::States& states, const std::vector<Matrix>& matrices,
                  const StepInput& input, const std::map<std::string, std::array<float, 3>>& impulses,
                  std::vector<PhysicsContactEvent>& events)
        {
            const auto& [seconds, horizontal, vertical, jump] = input;
            SynchronizeBodies(layout, matrices, seconds, impulses);
            Engine::CpuScope simulationScope("Physics simulation");
            const int steps = std::max(1, static_cast<int>(std::ceil(seconds * 120)));
            const float tick = seconds / static_cast<float>(steps);
            const float magnitude = std::max(1.0f, std::hypot(horizontal, vertical));
            for (int step = 0; step < steps; ++step)
            {
                for (auto& [id, entry] : entries)
                    if (entry.character)
                    {
                        UpdateCharacter(layout, states, matrices, id, entry,
                                        {horizontal, vertical, magnitude, tick, step, jump});
                    }
                if (system.Update(tick, 1, &temporary, &jobs) != JPH::EPhysicsUpdateError::None)
                    throw std::runtime_error("Physics capacity exceeded");
                Gather(events);
            }
            std::transform(contacts.begin(), contacts.end(), std::back_inserter(events),
                           [](const auto& pair)
                           {
                               return PhysicsContactEvent{std::get<0>(pair), std::get<1>(pair), "Stay",
                                                          std::get<2>(pair)};
                           });
            ApplyPhysicsTransforms(layout);
        }
    };
    struct PhysicsWorld::Transaction::State
    {
        std::shared_ptr<Impl> world;
        JPH::StateRecorderImpl system;
        std::map<std::string, JPH::StateRecorderImpl> characters;
        std::map<std::string, Pose> poses;
        std::set<Contact> contacts;
        std::map<std::string, std::array<float, 3>> impulses;
        std::map<std::string, ComponentInstanceHandle> impulseOwners;
        std::vector<PhysicsContactEvent> events;
    };
    PhysicsWorld::Transaction::Transaction(PhysicsWorld& world) : world_(&world), state_(std::make_unique<State>())
    {
        Engine::CpuScope scope("Physics rollback snapshot");
        state_->world = world.impl_;
        state_->impulses = world.impulses_;
        state_->impulseOwners = world.impulseOwners_;
        state_->events = world.events_;
        if (!state_->world)
            return;
        auto& saved = *state_->world;
        saved.system.SaveState(state_->system);
        state_->contacts = saved.contacts;
        for (const auto& [id, entry] : saved.entries)
        {
            state_->poses[id] = entry.pose;
            if (entry.character)
                entry.character->SaveState(state_->characters[id]);
        }
    }
    PhysicsWorld::Transaction::Transaction(Transaction&& other) noexcept
        : world_(other.world_), state_(std::move(other.state_))
    {
        other.world_ = nullptr;
    }
    PhysicsWorld::Transaction::~Transaction()
    {
        if (!world_)
            return;
        world_->impl_ = state_->world;
        world_->impulses_ = std::move(state_->impulses);
        world_->impulseOwners_.swap(state_->impulseOwners);
        world_->events_ = std::move(state_->events);
        if (!state_->world)
            return;
        auto& saved = *state_->world;
        state_->system.Rewind();
        saved.system.RestoreState(state_->system);
        saved.contacts = std::move(state_->contacts);
        for (auto& [id, entry] : saved.entries)
        {
            entry.pose = state_->poses.at(id);
            if (entry.character)
            {
                state_->characters.at(id).Rewind();
                entry.character->RestoreState(state_->characters.at(id));
            }
        }
    }
    void PhysicsWorld::Transaction::Commit() noexcept
    {
        world_ = nullptr;
    }
    PhysicsWorld::Transaction PhysicsWorld::BeginTransaction()
    {
        return Transaction(*this);
    }
    PhysicsWorld::PhysicsWorld()
    {
        Register();
    }
    PhysicsWorld::~PhysicsWorld() = default;
    void PhysicsWorld::Reset()
    {
        impl_.reset();
        rootQueries_.reset();
        impulses_.clear();
        impulseOwners_.clear();
        events_.clear();
    }
    bool PhysicsWorld::ConstrainFrame(const PhysicsFrame& before, PhysicsFrame& candidate,
                                      const std::vector<size_t>& actors, const std::filesystem::path& root,
                                      std::string& error) const
    {
        Engine::CpuScope scope("Root collision queries");
        try
        {
            const auto solid = [&](size_t index)
            {
                const auto& c = candidate.objects.at(index).boxCollider;
                return c && c->enabled && !c->isTrigger;
            };
            if (std::none_of(actors.begin(), actors.end(), solid))
            {
                error.clear();
                return true;
            }
            std::vector<Matrix> start, desired;
            if (!PhysicsTransforms::Resolve(before, start, error) ||
                !PhysicsTransforms::Resolve(candidate, desired, error))
                return false;
            if (!rootQueries_ || rootQueryRoot_ != root || !QueryFilesMatch(rootQueryFiles_) ||
                !rootQueries_->Matches(before, start))
            {
                Engine::CpuScope rebuild("Root collision world rebuild");
                auto replacement = std::make_shared<Impl>(before);
                auto files = QueryGeometryFiles(before, root);
                replacement->Build(start, root, nullptr, {});
                if (!QueryFilesMatch(files))
                    throw std::runtime_error("Root collider model changed during import");
                rootQueries_ = std::move(replacement);
                rootQueryRoot_ = root;
                rootQueryFiles_ = std::move(files);
            }
            auto& world = *rootQueries_;
            world.SynchronizeQueries(before, start);
            auto resolved = candidate;
            auto ordered = actors;
            std::stable_sort(ordered.begin(), ordered.end(),
                             [&](size_t first, size_t second)
                             {
                                 return ObjectDepth(before, first) < ObjectDepth(before, second);
                             });
            for (const auto index : ordered)
                if (solid(index))
                {
                    if (!world.ConstrainActor(candidate, resolved, start, desired, index, root, error))
                        return false;
                }
            candidate = std::move(resolved);
            error.clear();
            return true;
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return false;
        }
    }
    bool PhysicsWorld::AddImpulse(const std::string& id, const std::array<float, 3>& impulse)
    {
        if (!Finite(impulse) || (!impulses_.contains(id) && impulses_.size() >= 4096))
            return false;
        const auto found = impulses_.find(id);
        auto value = found == impulses_.end() ? std::array<float, 3>{} : found->second;
        for (size_t i = 0; i < 3; ++i)
            value[i] += impulse[i];
        if (!Finite(value) || std::any_of(value.begin(), value.end(),
                                          [](float f)
                                          {
                                              return std::abs(f) > 1000000;
                                          }))
            return false;
        impulses_[id] = value;
        return true;
    }
    bool PhysicsWorld::AddImpulse(WorldState::ReadView view, EntityHandle entity, const std::array<float, 3>& impulse)
    {
        const auto body = view.ComponentLease<RigidBodyComponent>(entity);
        const auto instance = view.Instance<RigidBodyComponent>(entity);
        if (!body || !body->enabled || body->motion != "dynamic" || !instance || !Finite(impulse))
            return false;
        const auto id = *view.ObjectId(entity);
        const auto previous = impulseOwners_.find(id);
        auto value = previous != impulseOwners_.end() && previous->second == *instance && impulses_.contains(id)
                         ? impulses_.at(id)
                         : std::array<float, 3>{};
        for (size_t axis = 0; axis < 3; ++axis)
            value[axis] += impulse[axis];
        if (!Finite(value) ||
            std::any_of(value.begin(), value.end(),
                        [](float number)
                        {
                            return std::abs(number) > 1000000;
                        }) ||
            (!impulses_.contains(id) && impulses_.size() >= 4096))
            return false;
        std::map<std::string, std::array<float, 3>> stagedValues{{id, value}};
        std::map<std::string, ComponentInstanceHandle> stagedOwners{{id, *instance}};
        if (const auto found = impulses_.find(id); found != impulses_.end())
            found->second = value;
        else
            impulses_.insert(stagedValues.extract(stagedValues.begin()));
        if (previous != impulseOwners_.end())
            previous->second = *instance;
        else
            impulseOwners_.insert(stagedOwners.extract(stagedOwners.begin()));
        return true;
    }
    bool PhysicsWorld::PrepareScene(WorldState::ReadView view, const ScenePhysics::States& states,
                                    const std::filesystem::path& root, std::string& error, bool reloadAssets)
    {
        try
        {
            std::erase_if(impulseOwners_,
                          [&](const auto& entry)
                          {
                              if (view.Alive(entry.second))
                                  return false;
                              impulses_.erase(entry.first);
                              return true;
                          });
            if (!impl_ && view.With<BoxColliderComponent>().empty())
            {
                error.clear();
                return true;
            }
            const auto frame = PhysicsFrame::Capture(view);
            std::vector<Matrix> matrices;
            if (!PhysicsTransforms::Resolve(frame, matrices, error))
                return false;
            if (reloadAssets || !impl_ || !impl_->Matches(frame, matrices))
            {
                auto replacement = std::make_shared<Impl>(frame);
                replacement->Build(matrices, root, impl_.get(), states);
                impl_.swap(replacement);
            }
            else
                impl_->SynchronizeQueries(frame, matrices);
            error.clear();
            return true;
        }
        catch (const std::exception& failure)
        {
            error = failure.what();
            return false;
        }
    }
    bool PhysicsWorld::AdvanceFrame(PhysicsFrame& layout, ScenePhysics::States& states, double seconds,
                                    float horizontal, float vertical, bool jump, const std::filesystem::path& root,
                                    std::string& error, const SceneLayout* legacyValidation, bool hasSavepoint)
    {
        if (!std::isfinite(seconds) || seconds <= 0 || !std::isfinite(horizontal) || !std::isfinite(vertical))
        {
            error = "Invalid physics input";
            return false;
        }
        Engine::CpuScope advanceScope("Physics advance");
        std::vector<Matrix> matrices;
        if (!PhysicsTransforms::Resolve(layout, matrices, error))
            return false;
        std::vector<TransformBackup> transforms;
        Impl::AdvanceBackup backup;
        bool saved = false;
        try
        {
            ValidateControllers(layout);
            auto candidateStates = states;
            // Step が書き換えるのは物理配置だけなので、全コンポーネントを複製せずに該当フィールドだけを保存します。
            if (!hasSavepoint)
                transforms = CaptureTransforms(layout);
            std::unique_ptr<Impl> replacement;
            auto* activeWorld = impl_.get();
            if (!activeWorld || !activeWorld->Matches(layout, matrices))
            {
                Engine::CpuScope rebuildScope("Physics world rebuild");
                if (legacyValidation)
                    static_cast<void>(legacyValidation->Serialize());
                replacement = std::make_unique<Impl>(layout);
                replacement->Build(matrices, root, impl_.get(), states);
                activeWorld = replacement.get();
            }
            auto& world = *activeWorld;
            if (!replacement && !hasSavepoint)
            {
                world.SaveAdvance(backup);
                saved = true;
            }
            std::vector<PhysicsContactEvent> events;
            world.Step(layout, candidateStates, matrices,
                       {static_cast<float>(std::min(seconds, 0.1)), std::clamp(horizontal, -1.0f, 1.0f),
                        std::clamp(vertical, -1.0f, 1.0f), jump},
                       impulses_, events);
            if (!PhysicsTransforms::Resolve(layout, matrices, error))
                throw std::runtime_error(error);
            if (replacement)
                impl_ = std::move(replacement);
            states = std::move(candidateStates);
            events_ = std::move(events);
            impulses_.clear();
            impulseOwners_.clear();
            error.clear();
            return true;
        }
        catch (const std::exception& exception)
        {
            RestoreTransforms(layout, transforms);
            if (saved)
            {
                impl_->RestoreAdvance(backup);
            }
            error = exception.what();
            return false;
        }
    }
    std::optional<PhysicsRayHit> PhysicsWorld::Raycast(const std::array<float, 3>& origin,
                                                       const std::array<float, 3>& direction, float distance,
                                                       unsigned int mask, bool includeTriggers,
                                                       const std::string& ignore) const
    {
        if (!impl_ || !Finite(origin) || !Finite(direction) || !std::isfinite(distance) || distance <= 0 ||
            distance > 100000)
            return {};
        auto vector = V(direction);
        if (vector.LengthSq() < 1e-12f)
            return {};
        const JPH::RRayCast ray(V(origin), vector.Normalized() * distance);
        JPH::RayCastResult hit;
        if (!impl_->system.GetNarrowPhaseQuery().CastRay(ray, hit, {}, {},
                                                         Impl::Filter(*impl_, SIZE_MAX, mask, includeTriggers, ignore)))
            return {};
        JPH::BodyLockRead lock(impl_->system.GetBodyLockInterface(), hit.mBodyID);
        if (!lock.Succeeded())
            return {};
        const auto& object = impl_->definition.objects.at(static_cast<size_t>(lock.GetBody().GetUserData()));
        const auto position = ray.GetPointOnRay(hit.mFraction);
        return PhysicsRayHit{object.id, distance * hit.mFraction, A(JPH::Vec3(position)),
                             A(lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, position)),
                             object.boxCollider->isTrigger};
    }

    bool PhysicsWorld::Advance(SceneLayout& layout, ScenePhysics::States& states, double seconds, float horizontal,
                               float vertical, bool jump, const std::filesystem::path& root, std::string& error)
    {
        auto frame = PhysicsFrame::Capture(layout);
        if (!AdvanceFrame(frame, states, seconds, horizontal, vertical, jump, root, error, &layout))
            return false;
        for (size_t index = 0; index < frame.objects.size(); ++index)
        {
            layout.objects[index].position = frame.objects[index].position;
            layout.objects[index].rotation = frame.objects[index].rotation;
            layout.objects[index].scale = frame.objects[index].scale;
        }
        return true;
    }
    bool PhysicsWorld::Advance(WorldState::Transaction& transaction, ScenePhysics::States& states, double seconds,
                               float horizontal, float vertical, bool jump, const std::filesystem::path& root,
                               std::string& error, const std::map<std::string, bool>& rootMotions)
    {
        auto rollback = BeginTransaction();
        auto candidateStates = states;
        if (!AdvanceCandidate(transaction, candidateStates, seconds, horizontal, vertical, jump, root, error,
                              rootMotions, rollback))
            return false;
        states.swap(candidateStates);
        rollback.Commit();
        return true;
    }
    bool PhysicsWorld::AdvanceCandidate(WorldState::Transaction& transaction, ScenePhysics::States& states,
                                        double seconds, float horizontal, float vertical, bool jump,
                                        const std::filesystem::path& root, std::string& error,
                                        const std::map<std::string, bool>& rootMotions, const Transaction& savepoint)
    {
        if (savepoint.world_ != this || !savepoint.state_)
            throw std::logic_error("Missing physics transaction savepoint");
        auto frame = PhysicsFrame::Capture(transaction.Read(), rootMotions);
        if (std::any_of(frame.objects.begin(), frame.objects.end(),
                        [](const auto& object)
                        {
                            return object.playerController && object.playerController->enabled &&
                                   !object.playerController->usePhysics;
                        }))
        {
            if (!ScenePhysics::AdvanceFrame(frame, states, seconds, horizontal, vertical, jump))
            {
                error = "Runtime player movement failed";
                return false;
            }
        }
        if (!AdvanceFrame(frame, states, seconds, horizontal, vertical, jump, root, error, nullptr, true))
            return false;
        for (const auto& object : frame.objects)
        {
            const LocalTransform value{object.position, object.rotation, object.scale};
            if (transaction.Read().Transform(object.entity) != value &&
                !transaction.SetLocalTransform(object.entity, value, error).succeeded)
                return false;
        }
        return true;
    }
    bool PhysicsWorld::ConstrainRootMotion(WorldState::Transaction& transaction,
                                           const std::vector<RuntimePose>& previous, const std::filesystem::path& root,
                                           std::string& error) const
    {
        auto candidate = PhysicsFrame::Capture(transaction.Read());
        auto before = candidate;
        std::vector<size_t> actors;
        for (const auto& pose : previous)
        {
            const auto found = std::find_if(before.objects.begin(), before.objects.end(),
                                            [&](const auto& item)
                                            {
                                                return item.entity == pose.entity;
                                            });
            if (found == before.objects.end())
            {
                error = "Root motion owner is stale";
                return false;
            }
            found->position = pose.transform.position;
            found->rotation = pose.transform.rotation;
            found->scale = pose.transform.scale;
            actors.push_back(static_cast<size_t>(found - before.objects.begin()));
        }
        if (!ConstrainFrame(before, candidate, actors, root, error))
            return false;
        for (const auto index : actors)
        {
            const auto& actor = candidate.objects[index];
            if (!transaction.SetLocalTransform(actor.entity, {actor.position, actor.rotation, actor.scale}, error)
                     .succeeded)
                return false;
        }
        return true;
    }
    bool PhysicsWorld::ConstrainRootMotion(const SceneLayout& before, SceneLayout& candidate,
                                           const std::vector<size_t>& actors, const std::filesystem::path& root,
                                           std::string& error) const
    {
        const auto start = PhysicsFrame::Capture(before);
        auto frame = PhysicsFrame::Capture(candidate);
        if (!ConstrainFrame(start, frame, actors, root, error))
            return false;
        for (size_t index = 0; index < frame.objects.size(); ++index)
        {
            candidate.objects[index].position = frame.objects[index].position;
            candidate.objects[index].rotation = frame.objects[index].rotation;
            candidate.objects[index].scale = frame.objects[index].scale;
        }
        return true;
    }
}
