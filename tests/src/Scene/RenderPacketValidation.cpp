#include <SceneRuntime/Rendering/RenderExtractor.h>
#include <Engine/Core/Profiler.h>
#include <cmath>
#include <iostream>

#if defined(__d3d12_h__) || defined(_WINDOWS_)
#error Render extraction must not depend on platform or rendering APIs
#endif

namespace
{
    using namespace SceneRuntime;
    size_t contracts = 0;
    void Require(bool value, const char* name)
    {
        ++contracts;
        if (!value)
            throw std::runtime_error(name);
        std::cout << "PASS: " << name << std::endl;
    }
    struct Resource final : RenderResource
    {
        int& destroyed;
        explicit Resource(int& value) : destroyed(value) {}
        ~Resource() override
        {
            ++destroyed;
        }
    };
    struct Resolver final : RenderResourceResolver
    {
        RenderBinding binding;
        unsigned int requests = 0;
        std::optional<RenderBinding> Resolve(EntityHandle, std::string&) override
        {
            ++requests;
            return binding;
        }
    };
    const RenderObject& Object(const RenderPacket& packet, const std::string& id)
    {
        return *std::find_if(packet.objects.begin(), packet.objects.end(),
                             [&](const auto& object)
                             {
                                 return object.id == id;
                             });
    }
    void Validate()
    {
        int destroyed = 0;
        Resolver resolver;
        resolver.binding = {7, 3, std::make_shared<Resource>(destroyed)};
        SceneLayout layout;
        ScenePlacement parent, child, hidden, lamp;
        parent.id = "parent";
        parent.position = {10, 0, 0};
        child.id = "child";
        child.parentId = "parent";
        child.position = {2, 0, 0};
        child.meshRenderer = MeshRendererComponent{};
        child.meshRenderer->model = "Assets/Models/unused.glb";
        child.meshRenderer->visibleWhen = "visible=1";
        child.meshRenderer->instancing = true;
        hidden = child;
        hidden.id = "hidden";
        hidden.meshRenderer->enabled = false;
        lamp.id = "lamp";
        lamp.position = {0, 4, 0};
        lamp.pointLight = PointLightComponent{};
        lamp.camera = CameraComponent{};
        layout.objects = {child, hidden, parent, lamp};
        auto world = std::make_unique<WorldState>(WorldDefinition::Compile(layout));
        std::string error;
        auto first = RenderExtractor::Extract(world->Read(), resolver, 8, 7, {}, 0, error);
        Require(first && first->frame == 8 && first->device == 7 && first->objects.size() == 1 &&
                    resolver.requests == 1,
                "packet-extracts-only-enabled-renderables");
        Require(Object(*first, "child").matrix._41 == 12 && Object(*first, "child").visibleWhen == "visible=1" &&
                    Object(*first, "child").instancing,
                "packet-resolves-out-of-order-hierarchy-and-render-flags");
        Require(first->cameras.size() == 1 && first->pointLights.size() == 1 &&
                    first->pointLights.front().matrix._42 == 4,
                "packet-captures-camera-and-light-transforms");
        const auto entity = *world->Read().Resolve("child");
        {
            auto tx = world->BeginTransaction();
            auto pose = *tx.Read().Transform(entity);
            pose.position[0] = 3;
            Require(tx.SetLocalTransform(entity, pose, error).succeeded, "packet-candidate-transform-prepared");
            auto candidate = RenderExtractor::Extract(tx.Read(), resolver, 9, 7, {}, 0, error);
            Require(candidate && Object(*candidate, "child").matrix._41 == 13 &&
                        Object(*first, "child").matrix._41 == 12 && world->Read().Transform(entity)->position[0] == 2,
                    "packet-candidate-does-not-change-prior-packet-or-world");
        }
        Require(world->Read().Transform(entity)->position[0] == 2, "packet-failed-frame-keeps-world");
        resolver.binding.device = 9;
        Require(!RenderExtractor::Extract(world->Read(), resolver, 9, 7, {}, 0, error) && !error.empty(),
                "packet-rejects-resource-from-other-device");
        resolver.binding.device = 7;
        resolver.binding.generation = 0;
        Require(!RenderExtractor::Extract(world->Read(), resolver, 9, 7, {}, 0, error),
                "packet-rejects-unpublished-resource-generation");
        resolver.binding.generation = 3;
        Require(!RenderExtractor::Extract(world->Read(), resolver, 9, 7, {}, std::numeric_limits<double>::quiet_NaN(),
                                          error),
                "packet-rejects-invalid-interpolation");
        auto resource = resolver.binding.resource;
        resolver.binding.resource.reset();
        Require(!RenderExtractor::Extract(world->Read(), resolver, 9, 7, {}, 0, error),
                "packet-rejects-unleased-resource");
        resolver.binding.resource = std::move(resource);
        resolver.binding.bounds.extent[0] = -1;
        Require(!RenderExtractor::Extract(world->Read(), resolver, 9, 7, {}, 0, error),
                "packet-rejects-invalid-bounds");
        resolver.binding.bounds.extent[0] = 0;
        {
            auto skin = std::make_shared<RenderSkin>();
            skin->bounds.extent = {1, 2, 3};
            resolver.binding.skin = skin;
            auto skinned = RenderExtractor::Extract(world->Read(), resolver, 9, 7, {}, 0, error);
            Require(skinned && skinned->objects.front().binding.bounds.extent[1] == 2,
                    "packet-uses-skin-bounds-with-owned-palette");
            Engine::SkinMatrix invalid;
            invalid.position._11 = std::numeric_limits<float>::quiet_NaN();
            auto invalidSkin = std::make_shared<RenderSkin>(*skin);
            invalidSkin->palettes = {{invalid}};
            resolver.binding.skin = invalidSkin;
            Require(!RenderExtractor::Extract(world->Read(), resolver, 9, 7, {}, 0, error),
                    "packet-rejects-nonfinite-skin-palette");
            Require(skinned->objects.front().binding.skin->palettes.empty(),
                    "packet-prior-skin-generation-remains-immutable");
            resolver.binding.skin.reset();
        }
        Engine::CpuProfiler::Take();
        auto second = RenderExtractor::Extract(world->Read(), resolver, 9, 7, {}, 0, error);
        const auto scopes = Engine::CpuProfiler::Take();
        Require(second && std::none_of(scopes.begin(), scopes.end(),
                                       [](const auto& scope)
                                       {
                                           return scope.name == "Runtime authoring export";
                                       }),
                "packet-extraction-does-not-export-authoring-layout");
        world.reset();
        resolver.binding.resource.reset();
        Require(destroyed == 0 && Object(*first, "child").matrix._41 == 12 &&
                    first->objects.front().binding.generation == 3,
                "packet-retains-data-and-resource-after-world-destruction");
        first.reset();
        Require(destroyed == 0, "packet-independent-view-keeps-resource");
        second.reset();
        Require(destroyed == 1, "packet-last-owner-releases-resource");
    }
    void ValidateInterpolation()
    {
        int destroyed = 0;
        Resolver resolver;
        resolver.binding = {1, 1, std::make_shared<Resource>(destroyed)};
        SceneLayout layout;
        ScenePlacement owner;
        owner.id = "body";
        owner.meshRenderer = MeshRendererComponent{};
        owner.meshRenderer->model = "Assets/Models/unused.glb";
        owner.boxCollider = BoxColliderComponent{};
        owner.rigidBody = RigidBodyComponent{};
        owner.rigidBody->motion = "dynamic";
        owner.position = {1, 0, 0};
        layout.objects.push_back(owner);
        WorldState world(WorldDefinition::Compile(layout));
        const auto entity = *world.Read().Resolve("body");
        RuntimePhysicalHistory history;
        RuntimePhysicalPose current{
            entity, {}, *world.Read().Instance<RigidBodyComponent>(entity), {}, *world.Read().Transform(entity)};
        auto previous = current;
        previous.transform.position[0] = 0;
        history.previous = {previous};
        history.current = {current};
        std::string error;
        auto packet = RenderExtractor::Extract(world.Read(), resolver, 1, 1, history, .25, error);
        Require(packet && packet->objects.front().matrix._41 == .25f &&
                    world.Read().Transform(entity)->position[0] == 1,
                "packet-interpolates-without-mutating-physical-pose");
        history.previous.front().transform.position[0] = -10;
        packet = RenderExtractor::Extract(world.Read(), resolver, 1, 1, history, .25, error);
        Require(packet && packet->objects.front().matrix._41 == 1, "packet-teleport-skips-interpolation");
        history.previous.front() = previous;
        history.current.front().body.generation += 100;
        packet = RenderExtractor::Extract(world.Read(), resolver, 1, 1, history, .25, error);
        Require(packet && packet->objects.front().matrix._41 == 1,
                "packet-component-generation-prevents-old-history-reuse");
    }
}
int main()
{
    try
    {
        Validate();
        ValidateInterpolation();
        std::cout << "Render packet contracts: " << contracts << ", failures: 0\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
