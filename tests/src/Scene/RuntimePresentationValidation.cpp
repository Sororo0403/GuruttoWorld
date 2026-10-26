#include "../../../SceneRuntime/src/Rendering/RuntimePresentationData.h"
#include <Engine/Core/Profiler.h>
#include <iostream>

#if defined(__d3d12_h__) || defined(_WINDOWS_)
#error Presentation definition extraction must not include platform or rendering APIs
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
    void Validate()
    {
        SceneLayout layout;
        ScenePlacement ui, sky, physical;
        ui.id = "ui";
        ui.canvas.emplace();
        ui.rectTransform.emplace();
        ui.image.emplace();
        ui.text.emplace();
        ui.meshRenderer.emplace();
        ui.meshRenderer->model = "Assets/Models/unused.glb";
        sky.id = "sky";
        sky.sky.emplace();
        physical.id = "body";
        physical.rigidBody.emplace();
        physical.boxCollider.emplace();
        layout.objects = {ui, sky, physical};
        for (unsigned int index = 0; index < 100; ++index)
        {
            ScenePlacement terrain;
            terrain.id = "terrain-" + std::to_string(index);
            terrain.terrain.emplace();
            layout.objects.push_back(std::move(terrain));
        }
        WorldState world(WorldDefinition::Compile(layout));
        Engine::CpuProfiler::Take();
        const auto definition = RuntimePresentationData::Extract(world.Read());
        Require(definition->objects.size() == 2 && definition->objects[0].id == "ui" &&
                    definition->objects[1].id == "sky",
                "presentation-definition-selects-ui-and-effects-in-stable-order");
        Require(definition->objects[0].image && definition->objects[0].text && !definition->objects[0].meshRenderer &&
                    !definition->objects[0].rigidBody,
                "presentation-definition-omits-mesh-physics-script-and-prefab-data");
        const auto scopes = Engine::CpuProfiler::Take();
        Require(std::none_of(scopes.begin(), scopes.end(),
                             [](const auto& scope)
                             {
                                 return scope.name == "Runtime authoring export";
                             }),
                "presentation-definition-never-exports-whole-world");
        Require(world.Read().Definition()->Source().objects.size() == 103 &&
                    world.Read().ComponentLease<MeshRendererComponent>(*world.Read().Resolve("ui")),
                "presentation-definition-keeps-world-and-source-immutable");
        std::string error;
        {
            auto tx = world.BeginTransaction();
            Require(tx.Prepare(error) && !RuntimePresentationData::Changed(tx.Changes()),
                    "presentation-noop-retains-definition-generation");
        }
        {
            auto tx = world.BeginTransaction();
            const auto entity = *tx.Read().Resolve("body");
            auto body = *tx.Read().ComponentLease<RigidBodyComponent>(entity);
            body.velocity[0] = 2;
            Require(tx.SetComponent(entity, std::optional{body}, error).succeeded && tx.Prepare(error) &&
                        !RuntimePresentationData::Changed(tx.Changes()),
                    "presentation-physics-value-write-keeps-ui-definition-generation");
        }
        {
            auto tx = world.BeginTransaction();
            const auto entity = *tx.Read().Resolve("ui");
            auto image = *tx.Read().ComponentLease<ImageComponent>(entity);
            image.color[0] = .25f;
            Require(tx.SetComponent(entity, std::optional{image}, error).succeeded && tx.Prepare(error) &&
                        RuntimePresentationData::Changed(tx.Changes()),
                    "presentation-image-value-write-invalidates-ui-definition");
            const auto candidate = RuntimePresentationData::Extract(tx.Read());
            Require(candidate->objects[0].image->color[0] == .25f && definition->objects[0].image->color[0] != .25f,
                    "presentation-candidate-retains-prior-ui-generation");
        }
        SceneChangeSet runtime;
        runtime.fields.push_back({{}, {}, "runtime.ui.values"});
        Require(!RuntimePresentationData::Changed(runtime), "presentation-ui-command-reuses-source-definition");
        runtime.fields.push_back({{}, {}, "settings.postEffects"});
        Require(RuntimePresentationData::Changed(runtime), "presentation-settings-change-invalidates-definition");
        SceneChangeSet structure;
        structure.structureChanged = true;
        Require(RuntimePresentationData::Changed(structure), "presentation-structural-change-invalidates-definition");
    }
}
int main()
{
    try
    {
        Validate();
        std::cout << "Runtime presentation contracts: " << contracts << ", failures: 0\n";
        return 0;
    }
    catch (const std::exception& failure)
    {
        std::cerr << "FAIL: " << failure.what() << '\n';
        return 1;
    }
}
