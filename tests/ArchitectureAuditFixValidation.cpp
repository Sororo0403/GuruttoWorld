#include "ArchitectureAuditFixValidation.h"
#include "ReauditFixValidation.h"
#include "../Editor/src/GameSession.h"
#include "../Editor/src/SceneDocument.h"
#include "../Editor/src/MaterialDocument.h"
#include <SceneRuntime/GenreGeometry.h>
#include <SceneRuntime/ProjectSettings.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <Engine/Platform/Window.h>
#include <d3d12sdklayers.h>
#include <iostream>

namespace
{
    using namespace SceneRuntime;

    void Require(bool value, const std::string& message)
    {
        if (!value)
            throw std::runtime_error(message);
    }

    ScenePlacement Actor(const std::string& id, const std::string& behaviour)
    {
        ScenePlacement object;
        object.id = id;
        object.scripts.push_back({"script", true, behaviour, {}});
        return object;
    }

    void ValidateProjectConflict(const std::filesystem::path& root)
    {
        ProjectSettings initial;
        initial.startupScene = "Assets/Scenes/Empty.json";
        initial.Save(root);
        auto first = ProjectSettings::Load(root);
        auto second = ProjectSettings::Load(root);
        second.width = 1920;
        second.Save(root);
        const auto saved = Engine::FileContents::Read(root / "Assets/Project.json");
        first.title = "Draft title";
        bool rejected = false;
        try
        {
            first.Save(root);
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        Require(rejected && first.title == "Draft title" &&
                    Engine::FileContents::Read(root / "Assets/Project.json") == saved,
                "stale project settings preserve external contents and draft");
        auto current = ProjectSettings::Load(root);
        current.height = 1080;
        current.Save(root);
        current.title = "Second save";
        current.Save(root);
        Require(ProjectSettings::Load(root).height == 1080, "successful saves update their conflict baseline");
        Engine::FileContents::Remove(root / "Assets/Project.json");
        rejected = false;
        try
        {
            current.Save(root);
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        Require(rejected && !std::filesystem::exists(root / "Assets/Project.json"),
                "external deletion is not silently recreated");
        initial = ProjectSettings{};
        initial.startupScene = "Assets/Scenes/Empty.json";
        initial.Save(root);
        std::cout << "PASS: project conflict, deletion, retry and saved baseline\n";
    }

    void ValidateDocumentSave(const std::filesystem::path& root)
    {
        const auto scene = root / "Assets/Scenes/Empty.json";
        Editor::SceneDocument document(scene);
        const auto original = Engine::FileContents::Read(scene);
        MaterialAsset material;
        material.Save(root, "Assets/Materials/A.mat");
        material.Save(root, "Assets/Materials/B.mat");
        Editor::MaterialDocument first, second;
        first.Reload(root, "Assets/Materials/A.mat");
        second.Reload(root, "Assets/Materials/B.mat");
        first.Asset().values.roughness = .8f;
        second.Asset().values.roughness = .9f;
        material.values.roughness = .2f;
        material.Save(root, "Assets/Materials/B.mat");
        SceneLayout changed;
        ScenePlacement object;
        object.id = "saved-object";
        changed.objects = {object};
        bool rejected = false;
        try
        {
            std::vector<Editor::DocumentSavePlan> plans;
            plans.push_back(document.PrepareSave(changed));
            plans.push_back(first.PrepareSave(root, "Assets/Materials/A.mat"));
            plans.push_back(second.PrepareSave(root, "Assets/Materials/B.mat"));
            Editor::DocumentSave::Apply(std::move(plans));
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        Require(rejected && Engine::FileContents::Read(scene) == original && first.Dirty() && second.Dirty() &&
                    MaterialAsset::Load(root, "Assets/Materials/A.mat").values.roughness == .5f,
                "later material conflict prevents every earlier save");
        second.Reload(root, "Assets/Materials/B.mat");
        second.Asset().values.roughness = .9f;
        const auto destination = root / "Assets/Scenes/SavedAs.json";
        const auto firstBefore = Engine::FileContents::Read(root / "Assets/Materials/A.mat");
        auto prepare = [&]()
        {
            std::vector<Editor::DocumentSavePlan> plans;
            plans.push_back(document.PrepareSaveAs(changed, destination, false));
            plans.push_back(first.PrepareSave(root, "Assets/Materials/A.mat"));
            plans.push_back(second.PrepareSave(root, "Assets/Materials/B.mat"));
            return plans;
        };
        auto plans = prepare();
        const auto locked = root / "Assets/Materials/B.mat";
        const auto handle = CreateFileW(locked.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                        FILE_ATTRIBUTE_NORMAL, nullptr);
        Require(handle != INVALID_HANDLE_VALUE, "material write failure lock");
        rejected = false;
        try
        {
            Editor::DocumentSave::Apply(std::move(plans));
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        CloseHandle(handle);
        Require(rejected && !std::filesystem::exists(destination) && document.Path() == scene && first.Dirty() &&
                    second.Dirty() && Engine::FileContents::Read(root / "Assets/Materials/A.mat") == firstBefore,
                "mid-write failure restores files, SaveAs identity and both saved baselines");
        Editor::DocumentSave::Apply(prepare());
        Require(document.Path() == destination && !first.Dirty() && !second.Dirty() &&
                    SceneLayout::Load(destination).objects.size() == 1 && Engine::FileContents::Read(scene) == original,
                "retry commits scene, materials and saved baselines together");
        document.Save(changed);
        std::cout << "PASS: all-document conflict, partial write rollback, SaveAs and retry\n";
    }

    struct Lifetime
    {
        int active = 0, starts = 0, stops = 0;
    };

    ScriptDefinition Counted(const std::shared_ptr<Lifetime>& counts)
    {
        ScriptDefinition definition;
        definition.start = [counts](ScriptContext&)
        {
            ++counts->active;
            ++counts->starts;
        };
        definition.stop = [counts](const ScriptStopContext&)
        {
            --counts->active;
            ++counts->stops;
        };
        definition.update = [](ScriptContext& context)
        {
            context.object.position[0] = ++context.state["ticks"];
        };
        return definition;
    }

    void ValidateSceneTransition(Engine::DirectX12Renderer& renderer, const std::filesystem::path& root)
    {
        const auto persistent = std::make_shared<Lifetime>();
        const auto removed = std::make_shared<Lifetime>();
        const auto incoming = std::make_shared<Lifetime>();
        const auto failure = std::make_shared<int>(0);
        Require(ScriptRegistry::Register("AuditPersistent", Counted(persistent)) &&
                    ScriptRegistry::Register("AuditRemoved", Counted(removed)),
                "transition definitions");
        auto definition = Counted(incoming);
        const auto start = definition.start;
        definition.start = [start, failure](ScriptContext& context)
        {
            start(context);
            if (*failure == 1)
                throw std::runtime_error("injected Start failure");
            if (*failure == 2)
                context.object.SetModel("Assets/Models/Missing.obj");
            context.scene->SetUiValue("started", 42);
        };
        Require(ScriptRegistry::Register("AuditIncoming", definition), "incoming definition");
        SceneLayout next;
        next.objects = {Actor("incoming", "AuditIncoming")};
        next.Save(root / "Assets/Scenes/Incoming.json");
        auto unknown = next;
        unknown.objects.front().scripts.front().behaviour = "AuditUnknown";
        unknown.Save(root / "Assets/Scenes/Unknown.json");
        {
            SceneLayout initial;
            initial.objects = {Actor("persistent", "AuditPersistent"), Actor("removed", "AuditRemoved")};
            initial.objects.front().persistent = true;
            SceneEnvironment environment;
            std::string error;
            Require(environment.Initialize(renderer, root, initial, error) && environment.StartScripts(error), error);
            Require(environment.Update(1.0 / 60, true, true), "transition initial update");
            environment.Ui().values["marker"] = 17;
            const auto before = environment.World().Layout().Serialize();
            const auto clock = environment.MotionSeconds();
            const auto revision = environment.SceneRevision();
            const auto ui = environment.Ui().values;
            for (const bool additive : {false, true})
            {
                Require(!environment.LoadScene("Assets/Scenes/Unknown.json", additive, error),
                        "unknown script rejected before transition");
                for (const int mode : {1, 2, 3, 4})
                {
                    *failure = mode;
                    const auto prepare = environment.World().UiPreparation();
                    if (mode >= 3)
                        environment.World().SetUiPreparation(
                            [mode](const auto&, const auto&, std::string& diagnostic)
                            {
                                if (mode == 4)
                                    throw std::runtime_error("injected transition preparation exception");
                                diagnostic = "injected transition UI failure";
                                return false;
                            });
                    Require(!environment.LoadScene("Assets/Scenes/Incoming.json", additive, error),
                            "Start and post-start resources must succeed before transition");
                    environment.World().SetUiPreparation(prepare);
                    Require(environment.World().Layout().Serialize() == before && environment.Ui().values == ui &&
                                environment.SceneRevision() == revision && environment.MotionSeconds() == clock &&
                                environment.Scenes().Entries().size() == 1 && persistent->active == 1 &&
                                removed->active == 1 && removed->stops == 0 && incoming->active == 0,
                            "failed transition preserves old world, UI, ownership, scripts and clocks");
                }
            }
            *failure = 0;
            Require(environment.LoadScene("Assets/Scenes/Incoming.json", true, error) &&
                        environment.Ui().Value("started") == 42 && environment.Scenes().Entries().size() == 2,
                    "additive transition commits Start UI commands");
            Require(environment.LoadScene("Assets/Scenes/Incoming.json", false, error) && persistent->starts == 1 &&
                        persistent->active == 1 && removed->active == 0 && removed->stops == 1 && incoming->active == 1,
                    "single transition keeps persistent instances and stops removed instances once");
            Require(environment.Update(1.0 / 60, true, true), "transition continues updating");
            Require(environment.World().Layout().objects.front().position[0] == 2,
                    "persistent script state survives transition");
        }
        Require(persistent->active == 0 && removed->active == 0 && incoming->active == 0,
                "transition resources released at shutdown");
        std::cout << "PASS: atomic single/additive transition, Start cleanup and persistent script lifetime\n";
    }

    void ValidateStopWriteback(Engine::DirectX12Renderer& renderer, const std::filesystem::path& root)
    {
        const auto stops = std::make_shared<int>(0);
        ScriptDefinition definition;
        definition.update = [](ScriptContext& context)
        {
            context.object.position[0] = 1;
            context.object.scripts.clear();
        };
        definition.prepareStop = [](ScriptContext& context)
        {
            context.object.position[1] += 1;
        };
        definition.stop = [stops](const ScriptStopContext&)
        {
            ++*stops;
        };
        Require(ScriptRegistry::Register("AuditStopWriteback", definition), "stop writeback definition");
        SceneLayout scene;
        scene.objects = {Actor("writeback", "AuditStopWriteback")};
        scene.objects.front().terrain.emplace();
        auto data = scene;
        ScriptRuntime runtime;
        std::string error;
        Require(runtime.Update(data, .016, error) && runtime.Update(data, .016, error) &&
                    data.objects.front().position == std::array<float, 3>{1, 1, 0} && *stops == 1,
                "standalone runtime preserves Stop writes to a surviving owner");
        runtime.Stop(data);
        Editor::GameSession session;
        Require(session.Play(renderer, root, scene, error, root / "writeback-settings.json") &&
                    session.Update(.016, true) && session.Update(.016, true) &&
                    session.Runtime()->World().Layout().objects.front().position == std::array<float, 3>{1, 1, 0} &&
                    *stops == 2,
                "frame commit preserves Stop writeback exactly once");
        Require(session.Runtime()->World().PickRay({2, 4, 1}, {0, -1, 0}, 3.5f) == "writeback",
                "Stop writeback updates projected mesh transforms before picking and drawing");
        Require(renderer.WaitForIdle() && session.Stop() && *stops == 2, "shutdown does not repeat a self removal");
    }

    void ValidateFrame(Engine::DirectX12Renderer& renderer, const std::filesystem::path& root)
    {
        const auto failure = std::make_shared<int>(0);
        const auto victim = std::make_shared<Lifetime>();
        Require(ScriptRegistry::Register("AuditVictim", Counted(victim)), "victim definition");
        ScriptDefinition driver;
        driver.fixedUpdate = [](ScriptContext& context)
        {
            context.object.position[0] = ++context.state["fixed"];
            context.scene->SetUiValue("fixed", context.object.position[0]);
            context.scene->AudioCue("fixed");
            if (context.scene->Find("victim"))
                context.scene->Destroy("victim");
        };
        driver.update = [failure](ScriptContext&)
        {
            if (*failure == 1)
                throw std::runtime_error("injected Update failure");
        };
        driver.lateUpdate = [failure](ScriptContext&)
        {
            if (*failure == 2)
                throw std::runtime_error("injected LateUpdate failure");
        };
        Require(ScriptRegistry::Register("AuditDriver", driver), "frame definition");
        SceneLayout scene;
        scene.objects = {Actor("driver", "AuditDriver"), Actor("victim", "AuditVictim")};
        ScenePlacement body;
        body.id = "body";
        body.position[1] = 10;
        body.rigidBody.emplace();
        body.boxCollider.emplace();
        scene.objects.push_back(body);
        std::string error;
        Editor::GameSession session;
        Require(session.Play(renderer, root, scene, error, root / "user-settings.json"), error);
        int audio = 0;
        session.Runtime()->SetAudioCommandHandler(
            [&](const auto&)
            {
                ++audio;
            });
        const auto before = session.Runtime()->World().Layout().Serialize();
        const auto ui = session.Runtime()->Ui().values;
        const auto prepare = session.Runtime()->World().UiPreparation();
        Engine::InputActions actions;
        Engine::InputSnapshot snapshot;
        snapshot.active = true;
        actions.Update(snapshot);
        for (const int mode : {1, 2, 3})
        {
            *failure = mode;
            if (mode == 3)
                session.Runtime()->World().SetUiPreparation(
                    [](const auto&, const auto&, std::string& diagnostic)
                    {
                        diagnostic = "injected frame UI failure";
                        return false;
                    });
            session.PrepareInput(actions, true, .1, &snapshot);
            Require(session.Runtime()->World().Layout().Serialize() == before,
                    "input preparation cannot commit physics");
            Require(!session.Update(.1, true) && session.Runtime()->World().Layout().Serialize() == before &&
                        session.State().Elapsed() == 0 && session.Runtime()->MotionSeconds() == 0 &&
                        session.Runtime()->Ui().values == ui && victim->active == 1 && victim->stops == 0 && audio == 0,
                    "failed frame restores all phases, lifecycle, UI, audio and clocks");
            session.Runtime()->World().SetUiPreparation(prepare);
        }
        *failure = 0;
        session.PrepareInput(actions, true, 1.0 / 120, &snapshot);
        Require(session.Update(1.0 / 120, true) && audio == 0 && victim->active == 1,
                "retry restores fixed accumulator and deferred input");
        session.PrepareInput(actions, true, 1.0 / 120, &snapshot);
        Require(session.Update(1.0 / 120, true) && audio == 1 && victim->active == 0 && victim->stops == 1 &&
                    session.Runtime()->World().Layout().objects.front().position[0] == 1 &&
                    session.Runtime()->Ui().Value("fixed") == 1,
                "retry commits exactly one fixed tick, lifecycle and audio command");
        SceneLayout bodyOnly;
        bodyOnly.objects = {body};
        SceneEnvironment reference;
        Require(reference.Initialize(renderer, root, bodyOnly, error) && reference.Update(1.0 / 60, true, true),
                "reference physics frame");
        const auto& actual = session.Runtime()->World().Layout().objects.back().position;
        const auto& expected = reference.World().Layout().objects.front().position;
        Require(std::abs(actual[1] - expected[1]) < 1e-5f, "failed frames preserve Jolt body velocity and position");
        Require(session.Pause() && session.Step(), "pause and step keep common frame transaction");
        Require(renderer.WaitForIdle() && session.Stop(), "frame session shutdown");
        ValidateStopWriteback(renderer, root);
        std::cout << "PASS: frame rollback across Fixed/Update/Late/UI, physics, queues, Stop and retry\n";
    }

    void ValidateProceduralCache(Engine::DirectX12Renderer& renderer, const std::filesystem::path& root)
    {
        Engine::ModelManager models;
        Require(models.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), root / "Shaders/Mesh.hlsl"),
                "procedural cache initialization");
        int generated = 0;
        const auto generate = [&]()
        {
            ++generated;
            return std::vector<Engine::MeshData>{GenreGeometry::Terrain(TerrainComponent{}, root)};
        };
        auto first = models.Procedural("shared", "same", generate);
        auto second = models.Procedural("shared", "same", generate);
        Require(first && first == second && generated == 1, "live procedural resources are shared");
        auto replacement = models.Procedural("shared", "changed", generate);
        Require(replacement && replacement != first && generated == 2, "changed geometry replaces cache entry");
        std::vector<std::weak_ptr<const Engine::ModelRenderer>> released;
        for (int index = 0; index < 32; ++index)
        {
            auto model = models.Procedural("removed-" + std::to_string(index), "same", generate);
            Require(model != nullptr, "procedural resource upload");
            released.push_back(model);
        }
        Require(renderer.WaitForIdle() && std::all_of(released.begin(), released.end(),
                                                      [](const auto& resource)
                                                      {
                                                          return resource.expired();
                                                      }),
                "removed procedural resources are not retained by unique IDs");
        auto retirement = Engine::GpuRetirement::Shared(renderer.GetDevice(), renderer.GetCommandQueue());
        std::weak_ptr<const Engine::ModelRenderer> retired = replacement;
        Require(retirement && retirement->Retire(std::const_pointer_cast<Engine::ModelRenderer>(replacement)),
                "retire removed resource before dropping its live owner");
        replacement.reset();
        Require(renderer.WaitForIdle(), "retired resource GPU completion");
        retirement->Collect();
        Require(retired.expired(), "retired resource is released after fence completion");
        SceneLayout terrainScene;
        ScenePlacement terrain;
        terrain.id = "terrain";
        terrain.terrain.emplace();
        terrainScene.objects = {terrain};
        SceneWorld world;
        std::string error;
        Require(world.Initialize(renderer, root, terrainScene, root / "Shaders/Mesh.hlsl", &error), error);
        Engine::Camera camera;
        camera.SetPosition({0, 2, -4});
        Require(renderer.Render({0, 0, 0, 1},
                                [&](auto* commands, float)
                                {
                                    world.Draw(commands, camera, {});
                                }) != Engine::RenderResult::Failed &&
                    world.RemoveObjects({"terrain"}, error) && renderer.WaitForIdle(),
                "removed terrain remains alive until submitted drawing completes");
        std::cout << "PASS: procedural sharing, replacement, repeated removal and GPU retirement\n";
    }
}

namespace ArchitectureAuditFixValidation
{
    void Run()
    {
        const auto root = std::filesystem::absolute("generated/tests/architecture-audit-fixes") /
                          (std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
        std::filesystem::create_directories(root / "Assets/Scenes");
        std::filesystem::create_directories(root / "Assets/Materials");
        std::filesystem::copy("Content/Shaders", root / "Shaders", std::filesystem::copy_options::recursive);
        SceneLayout{}.Save(root / "Assets/Scenes/Empty.json");
        ValidateProjectConflict(root);
        ValidateDocumentSave(root);
        Engine::Window window;
        Engine::DirectX12Renderer renderer;
        Require(window.Create(L"Architecture audit regression", 64, 64) && renderer.Initialize(window.GetHandle()),
                "audit regression renderer");
        ValidateSceneTransition(renderer, root);
        ValidateFrame(renderer, root);
        ValidateProceduralCache(renderer, root);
        ReauditFixValidation::Lifecycle(renderer, root);
        ReauditFixValidation::Save(root);
        Require(renderer.WaitForIdle(), "audit GPU completion");
        Microsoft::WRL::ComPtr<ID3D12InfoQueue> messages;
        if (SUCCEEDED(renderer.GetDevice()->QueryInterface(IID_PPV_ARGS(&messages))))
        {
            for (UINT64 index = 0; index < messages->GetNumStoredMessages(); ++index)
            {
                SIZE_T size = 0;
                messages->GetMessage(index, nullptr, &size);
                std::vector<unsigned char> bytes(size);
                auto* message = reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
                Require(SUCCEEDED(messages->GetMessage(index, message, &size)) &&
                            message->Severity != D3D12_MESSAGE_SEVERITY_ERROR &&
                            message->Severity != D3D12_MESSAGE_SEVERITY_CORRUPTION,
                        "audit resource lifecycle has no D3D12 error or corruption");
            }
        }
    }
}

#ifndef WP1_AUDIT_EMBEDDED
int main()
{
    try
    {
        ArchitectureAuditFixValidation::Run();
        return 0;
    }
    catch (const std::exception& failure)
    {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
#endif
