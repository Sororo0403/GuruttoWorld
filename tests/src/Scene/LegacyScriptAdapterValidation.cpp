#include <LegacyScripts/LegacyScriptAdapter.h>
#include <LegacyScripts/ScriptRuntime.h>
#include <SceneRuntime/Scene/RuntimeSession.h>
#include <Engine/Core/Profiler.h>
#include <iostream>

#if defined(__d3d12_h__) || defined(_WINDOWS_)
#error Script migration adapter must not include rendering or platform APIs
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
        int starts = 0, stops = 0;
        bool fail = false;
        std::string error;
        ScriptDefinition first;
        first.start = [&](ScriptContext& context)
        {
            ++starts;
            context.state["ticks"] = 0;
        };
        first.update = [&](ScriptContext& context)
        {
            context.state["ticks"] += 1;
            context.object.position[0] = context.state["ticks"];
            Require(context.scene->Find(context.object.id) == &context.object &&
                        context.scene->Read().Transform(context.object.id)->position[0] == context.object.position[0],
                    "legacy-adapter-reads-local-owner-without-layout-copy");
            const auto* peer = context.scene->Find("peer");
            Require(peer && peer->position[0] == 10 && context.scene->Read().ObjectIds().size() == 2,
                    "legacy-adapter-resolves-other-runtime-entities");
            auto material = context.object.material;
            material->roughness = .2f;
            Require(context.SetComponent(&ScenePlacement::material, material).succeeded,
                    "legacy-adapter-queues-typed-component");
            context.scene->SetUiValue("ticks", context.state["ticks"]);
            if (fail)
                throw std::runtime_error("injected legacy callback failure");
        };
        first.stop = [&](const ScriptStopContext& context)
        {
            ++stops;
            Require(context.object.id == "owner" && context.object.position[1] == 7,
                    "legacy-adapter-stop-owns-final-owner");
        };
        first.prepareStop = [](ScriptContext& context)
        {
            Require(!context.scene, "legacy-adapter-stop-preparation-has-no-live-scene-writer");
            context.object.position[1] = 7;
        };
        ScriptDefinition second;
        second.update = [](ScriptContext& context)
        {
            auto light = context.object.pointLight;
            light->intensity = 5;
            Require(context.SetComponent(&ScenePlacement::pointLight, light).succeeded,
                    "legacy-adapter-composes-previous-component-request");
        };
        Require(ScriptRegistry::InstallModule("adapter-validation", {{"LegacyFirst", first}, {"LegacySecond", second}},
                                              error),
                "legacy-adapter-registers-source-generation");
        const auto captured = LegacyScriptAdapter::Capture();
        ScenePlacement owner, peer;
        owner.id = "owner";
        owner.material = MaterialComponent{};
        owner.pointLight = PointLightComponent{};
        ScriptComponent a, b;
        a.id = "a";
        a.behaviour = "LegacyFirst";
        b.id = "b";
        b.behaviour = "LegacySecond";
        owner.scripts = {a, b};
        peer.id = "peer";
        peer.position = {10, 0, 0};
        SceneLayout source;
        source.objects = {owner, peer};
        const auto definition = WorldDefinition::Compile(source);
        RuntimeSession session(definition, captured);
        Require(session.Start(error) && starts == 1, "legacy-adapter-starts-on-typed-runtime");
        Engine::CpuProfiler::Take();
        const auto entity = *session.Read().Resolve("owner");
        Require(session.Tick(.01, {}, false, error) && session.Read().Transform(entity)->position[0] == 1 &&
                    session.Read().Component<MaterialComponent>(entity)->roughness == .2f &&
                    session.Read().Component<PointLightComponent>(entity)->intensity == 5,
                "legacy-adapter-commits-values-and-composed-components");
        session.TakeChanges();
        fail = true;
        Require(!session.Tick(.01, {}, false, error) && session.Read().Transform(entity)->position[0] == 1 &&
                    session.TakeChanges().Empty() && stops == 0,
                "legacy-adapter-failure-keeps-published-state");
        fail = false;
        Require(session.Tick(.01, {}, false, error) && session.Read().Transform(entity)->position[0] == 2 &&
                    starts == 1,
                "legacy-adapter-retries-instance-state");
        bool exported = false;
        for (const auto& sample : Engine::CpuProfiler::Take())
            exported |= sample.name == "Runtime authoring export";
        Require(!exported, "legacy-adapter-does-not-export-whole-layout-per-callback");
        ScriptDefinition replacement;
        replacement.update = [](ScriptContext& context)
        {
            context.object.position[0] = 99;
        };
        Require(ScriptRegistry::InstallModule("adapter-validation",
                                              {{"LegacyFirst", replacement}, {"LegacySecond", second}}, error),
                "legacy-adapter-installs-next-generation");
        Require(session.Tick(.01, {}, false, error) && session.Read().Transform(entity)->position[0] == 3,
                "legacy-adapter-running-session-retains-captured-generation");
        RuntimeSession next(definition, LegacyScriptAdapter::Capture());
        Require(next.Tick(.01, {}, false, error) &&
                    next.Read().Transform(*next.Read().Resolve("owner"))->position[0] == 99,
                "legacy-adapter-new-session-uses-next-generation");
        Require(session.Edit(RemoveObjectsEdit{{"owner"}}, error) && stops == 1,
                "legacy-adapter-destroy-stops-after-successful-publication");
    }
}
int main()
{
    try
    {
        Validate();
        std::cout << "Legacy adapter contracts: " << contracts << ", failures: 0\n";
        return 0;
    }
    catch (const std::exception& failure)
    {
        std::cerr << "FAIL: " << failure.what() << '\n';
        return 1;
    }
}
