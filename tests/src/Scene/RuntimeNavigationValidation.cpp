#include <SceneRuntime/Gameplay/RuntimeNavigation.h>
#include <SceneRuntime/Scene/RuntimeSimulation.h>
#include "../Physics/NavigationTransformValidation.h"
#include <Engine/Core/Profiler.h>
#include <iostream>

#if defined(__d3d12_h__) || defined(_WINDOWS_)
#error Runtime navigation must not include platform or rendering APIs
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
    void Validate(bool reversed)
    {
        auto source = NavigationTransformValidation::Family(reversed);
        auto legacy = source;
        std::string error;
        Require(Navigation::Advance(legacy, 1, error), "runtime-navigation-reference-advances");
        WorldState world(WorldDefinition::Compile(source));
        Engine::CpuProfiler::Take();
        {
            auto transaction = world.BeginTransaction();
            Require(RuntimeNavigation::Advance(transaction, 1, error) && transaction.Prepare(error) &&
                        transaction.Commit(),
                    "runtime-navigation-advances-typed-world");
        }
        for (const auto& object : legacy.objects)
        {
            const auto entity = *world.Read().Resolve(object.id);
            const auto pose = *world.Read().Transform(entity);
            Require(std::hypot(pose.position[0] - object.position[0], pose.position[1] - object.position[1],
                               pose.position[2] - object.position[2]) < .00001f,
                    "runtime-navigation-matches-existing-hierarchy-movement");
        }
        const auto parent = *world.Read().Resolve("parent");
        const auto before = world.Read().Transform(parent);
        {
            auto transaction = world.BeginTransaction();
            Require(RuntimeNavigation::Advance(transaction, .25, error), "runtime-navigation-stages-second-movement");
        }
        Require(world.Read().Transform(parent) == before, "runtime-navigation-abort-preserves-published-hierarchy");
        bool exported = false;
        for (const auto& sample : Engine::CpuProfiler::Take())
            exported |= sample.name == "Runtime authoring export";
        Require(!exported, "runtime-navigation-does-not-export-whole-layout");
        RuntimeSession session(WorldDefinition::Compile(source));
        auto simulation = RuntimeSimulation::Create({});
        session.SetSystem(simulation.Pipeline());
        Require(session.Start(error) && session.Tick(source.settings.fixedStep, {}, true, error) &&
                    session.FixedTicks() == 1,
                "runtime-navigation-connects-to-common-simulation-pipeline");
    }
}
int main()
{
    try
    {
        Validate(false);
        Validate(true);
        std::cout << "Runtime navigation contracts: " << contracts << ", failures: 0\n";
        return 0;
    }
    catch (const std::exception& failure)
    {
        std::cerr << "FAIL: " << failure.what() << '\n';
        return 1;
    }
}
