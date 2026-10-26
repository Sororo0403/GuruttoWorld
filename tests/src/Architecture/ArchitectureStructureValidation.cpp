#include "../../../Editor/src/Documents/SceneDocument.h"
#include "../../../Editor/src/Inspector/ComponentEditResources.h"
#include <SceneRuntime/Scene/SceneEnvironment.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <iostream>

namespace
{
    // 構造上の契約が破られた場合に検証を中断します。
    void Require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    // 取得後の外部変更や読み取り不能が保存基準と正本を分離しないことを検証します。
    void ValidateDocumentOpen(const std::filesystem::path& root)
    {
        const auto path = root / "Assets/Scenes/Main.json";
        SceneRuntime::SceneLayout initial;
        SceneRuntime::ScenePlacement object;
        object.id = "loaded";
        initial.objects.push_back(object);
        const auto original = std::string("\xEF\xBB\xBF") + initial.Serialize();
        Engine::FileContents::Write(path, original);
        Editor::SceneDocument document(path);
        SceneRuntime::SceneWorld projection(document.Data());
        projection.SetInputActions({{"move", 1.0f}}, {{"jump", true}});
        projection.DiscardPendingInput();
        Require(projection.IsDocumentView() && !projection.MovePlayers(1.0 / 60, 1, 0) &&
                    !projection.PhysicsRaycast({0, 0, 0}, {0, 0, 1}, 10) && !projection.AddImpulse("loaded", {1, 0, 0}),
                "editing projection owns no executable script or physics session");
        auto candidate = Editor::SceneDocument::PrepareOpen(path);
        auto external = initial;
        external.objects.front().name = "external";
        external.Save(path);
        std::string error;
        Require(document.Apply(projection, root, std::move(candidate), error), error.c_str());
        Require(document.Layout().Serialize() == initial.Serialize(), "open uses captured source including BOM");
        bool rejected = false;
        try
        {
            document.Save(document.Layout());
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        Require(rejected && SceneRuntime::SceneLayout::Load(path).Serialize() == external.Serialize(),
                "external edits after capture remain a save conflict");

        auto captured = Editor::SceneDocument::PrepareOpen(path);
        std::filesystem::remove(path);
        std::filesystem::create_directory(path);
        Require(document.Apply(projection, root, std::move(captured), error) &&
                    document.Layout().Serialize() == external.Serialize(),
                "commit performs no second source read after projection exchange");
        const auto before = document.Layout().Serialize();
        const auto beforePath = document.Path();
        Require(!document.Open(projection, root, path, error) && !error.empty() && document.Path() == beforePath &&
                    document.Layout().Serialize() == before,
                "read failure preserves document and projection");
        std::filesystem::remove(path);
        Engine::FileContents::Write(path, "invalid json");
        Require(!document.Open(projection, root, path, error) && document.Path() == beforePath &&
                    document.Layout().Serialize() == before,
                "parse failure preserves document and projection");

        external.Save(path);
        auto wrongRoot = Editor::SceneDocument::PrepareOpen(path);
        Require(!document.Apply(projection, root / "Other", std::move(wrongRoot), error) &&
                    document.Layout().Serialize() == before && document.Path() == beforePath,
                "projection rejection preserves document and save baseline");
        document.Save(document.Layout());
        std::cout << "PASS: captured scene source, external conflict, BOM, read/parse/projection failure\n";
    }

    // 編集側の同期判断が、描画側の資源変更規則に従うことを検証します。
    void ValidateResourceChanges()
    {
        SceneRuntime::ScenePlacement before;
        before.id = "resource";
        SceneRuntime::SceneLayout layout;
        layout.objects.push_back(before);
        Editor::ObjectRequest request;
        request.action = Editor::ObjectAction::Components;
        request.id = before.id;
        request.components = before;
        request.components->position[0] = 1;
        Require(!Editor::ComponentEditResources::NeedsIdle(layout, request),
                "transform-only edit does not require resource synchronization");
        request.components->text.emplace();
        Require(Editor::ComponentEditResources::NeedsIdle(layout, request), "adding text synchronizes resources");
        layout.objects.front() = *request.components;
        request.components->text->fontSize += 1;
        Require(Editor::ComponentEditResources::NeedsIdle(layout, request),
                "text raster changes synchronize resources");
        request.components = before;
        request.components->material.emplace();
        Require(Editor::ComponentEditResources::NeedsIdle(layout, request),
                "material replacement synchronizes resources");
        std::cout << "PASS: renderer-owned resource-change rules drive editor synchronization\n";
    }

    // 同名のスクリプト定義を交換しても、既存セッションの動作と寿命が変わらないことを検証します。
    void ValidateScriptCatalogs()
    {
        SceneRuntime::ScriptDefinition definition;
        definition.update = [](SceneRuntime::ScriptContext& context)
        {
            context.object.position[0] = ++context.state["ticks"];
        };
        std::string error;
        Require(
            SceneRuntime::ScriptRegistry::InstallModule("structure-catalog", {{"StructureCatalog", definition}}, error),
            error.c_str());
        SceneRuntime::ScriptRuntime first(SceneRuntime::ScriptRegistry::Capture());
        definition.update = [](SceneRuntime::ScriptContext& context)
        {
            context.object.position[0] = (context.state["ticks"] += 10);
        };
        Require(
            SceneRuntime::ScriptRegistry::InstallModule("structure-catalog", {{"StructureCatalog", definition}}, error),
            error.c_str());
        SceneRuntime::ScriptRuntime second(SceneRuntime::ScriptRegistry::Capture());
        Require(SceneRuntime::ScriptRegistry::InstallModule("structure-catalog", {}, error), error.c_str());
        SceneRuntime::SceneLayout one;
        one.objects.emplace_back().id = "catalog";
        one.objects.front().scripts.emplace_back().behaviour = "StructureCatalog";
        auto two = one;
        Require(first.StartCandidate(one, error) && second.StartCandidate(two, error), error.c_str());
        first.CommitStarts();
        second.CommitStarts();
        Require(first.Update(one, 1.0 / 60, error) && second.Update(two, 1.0 / 60, error) &&
                    one.objects.front().position[0] == 1 && two.objects.front().position[0] == 10,
                "independent snapshots retain same-name definitions after global module removal");
        Require(SceneRuntime::ScriptRuntime::HasPhase(one, SceneRuntime::ScriptPhase::Update, &first.Definitions()) &&
                    !SceneRuntime::ScriptRuntime::HasPhase(one, SceneRuntime::ScriptPhase::Update),
                "phase selection uses the session catalog rather than global registration");
        first.Stop(one);
        second.Stop(two);
        std::cout << "PASS: independent script catalogs survive same-name replacement and module removal\n";
    }

    // シーン操作の個別確定・結果通知と明示的なGPU同期を実環境で検証します。
    // 固定・通常・後段更新を外部へ途中公開せず、一度だけ準備して確定する契約を検証します。
    void ValidateFramePreparation(Engine::DirectX12Renderer& renderer, const std::filesystem::path& root)
    {
        using namespace SceneRuntime;
        ScriptDefinition definition;
        definition.fixedUpdate = [](ScriptContext& context)
        {
            context.object.position[0] = ++context.state["fixed"];
        };
        definition.update = [](ScriptContext& context)
        {
            context.object.position[1] = ++context.state["update"];
            context.scene->SetUiValue("staged", 1);
            context.scene->AudioCue("staged");
        };
        definition.lateUpdate = [](ScriptContext& context)
        {
            context.object.position[2] = ++context.state["late"];
        };
        Require(ScriptRegistry::Register("StructureStagedFrame", definition), "staged frame definition");
        SceneLayout layout;
        ScenePlacement actor;
        actor.id = "staged";
        actor.scripts.emplace_back().behaviour = "StructureStagedFrame";
        layout.objects.push_back(actor);
        SceneWorld world;
        std::string error;
        Require(world.Initialize(renderer, root, layout, root / "Engine/Shaders/Mesh.hlsl", &error) &&
                    world.StartScripts(error),
                error.c_str());
        size_t preparations = 0;
        bool rejectRuntime = true, rejectUi = false;
        world.SetRuntimePreparation(
            [&](const SceneLayout& candidate, std::string& diagnostic)
            {
                ++preparations;
                Require(world.Layout().objects.front().position == std::array<float, 3>{},
                        "no fixed-step intermediate commit is visible to preparation");
                Require(candidate.objects.front().position == std::array<float, 3>{1, 1, 1},
                        "all update phases share a candidate and rolled-back script values");
                Require(!world.UpdateFrame(1.0 / 60, 0, 0, false, true), "recursive update is rejected");
                auto settings = world.Layout().settings;
                settings.gravity[1] = -1;
                std::string rejected;
                Require(!world.SetSettings(settings, rejected) && !rejected.empty() &&
                            !world.RenameObject("staged", "uncommitted") &&
                            !world.SetLocalTransform("staged", {9, 9, 9}, {}, {1, 1, 1}) &&
                            !world.RemoveObjects({"staged"}, rejected) &&
                            !world.ReplaceLayout(candidate, root, rejected, true),
                        "preparation cannot edit or replace the committed scene");
                Require(world.TakeSceneCommands().empty() && world.TakeAudioCommands().empty() &&
                            world.TakeUiCommands().values.empty(),
                        "preparation cannot drain unpublished commands");
                if (rejectRuntime)
                    diagnostic = "injected runtime rejection";
                return !rejectRuntime;
            });
        world.SetUiPreparation(
            [&](const auto&, const ScriptUiCommands& commands, std::string&)
            {
                Require(commands.values.at("staged") == 1, "preparation cannot consume candidate UI commands");
                return !rejectUi;
            });
        Require(!world.UpdateFrame(1.0 / 60, 0, 0, false, true) && preparations == 1,
                "runtime failure rejects the single candidate");
        rejectRuntime = false;
        rejectUi = true;
        Require(!world.UpdateFrame(1.0 / 60, 0, 0, false, true) && preparations == 2 &&
                    world.Layout().objects.front().position == std::array<float, 3>{},
                "UI rejection rolls back every phase");
        rejectUi = false;
        Require(world.UpdateFrame(1.0 / 60, 0, 0, false, true) && preparations == 3 &&
                    world.Layout().objects.front().position == std::array<float, 3>{1, 1, 1},
                "retry prepares once and commits every phase exactly once");
        Require(world.TakeAudioCommands().size() == 1 && world.TakeUiCommands().values.at("staged") == 1 &&
                    world.Layout().objects.front().name.empty() && world.Layout().settings.gravity[1] == -20,
                "commands publish once and rejected external writes leave committed data unchanged");
        world.SetRuntimePreparation({});
        world.SetUiPreparation({});
        Require(world.MovePlayers(1.0 / 60, 0, 0, false) && world.UpdateComponents(1.0 / 60) &&
                    world.Layout().objects.front().position == std::array<float, 3>{2, 2, 2},
                "standalone fixed/component entry points retain the same transaction boundary");
        std::cout << "PASS: single frame candidate, isolated preparation, reentrancy, rejection and retry\n";
    }

    void ValidateSceneCommands(const std::filesystem::path& root)
    {
        std::filesystem::create_directories(root / "Engine");
        std::filesystem::copy("Engine/Shaders", root / "Engine/Shaders", std::filesystem::copy_options::recursive);
        SceneRuntime::SceneLayout{}.Save(root / "Assets/Scenes/Incoming.json");
        Engine::Window window;
        Engine::DirectX12Renderer renderer;
        Require(window.Create(L"Structure validation", 64, 64) && renderer.Initialize(window.GetHandle()),
                "structure renderer");
        Microsoft::WRL::ComPtr<ID3D12Fence> marker;
        Require(SUCCEEDED(renderer.GetDevice()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&marker))) &&
                    SUCCEEDED(renderer.GetCommandQueue()->Signal(marker.Get(), 7)) &&
                    Engine::SynchronizeGpuQueue(renderer.GetDevice(), renderer.GetCommandQueue()) &&
                    marker->GetCompletedValue() == 7 && !Engine::SynchronizeGpuQueue(nullptr, nullptr),
                "explicit queue synchronization completes earlier work and rejects invalid arguments");
        SceneRuntime::ScriptDefinition definition;
        definition.update = [](SceneRuntime::ScriptContext& context)
        {
            if (context.state["requested"]++ == 0)
            {
                context.scene->LoadScene("Assets/Scenes/Missing.json", true);
                context.scene->LoadScene("Assets/Scenes/Incoming.json", true);
                context.scene->UnloadScene("Assets/Scenes/Incoming.json");
            }
        };
        const auto completed = std::make_shared<int>(0);
        const auto failed = std::make_shared<int>(0);
        definition.onEvent = [completed, failed](SceneRuntime::ScriptContext& context)
        {
            if (context.event->name == "sceneCommandCompleted")
                ++*completed;
            if (context.event->name == "sceneCommandFailed")
                ++*failed;
        };
        Require(SceneRuntime::ScriptRegistry::Register("StructureSceneCommands", std::move(definition)),
                "scene command definition");
        SceneRuntime::SceneLayout layout;
        SceneRuntime::ScenePlacement actor;
        actor.id = "commands";
        actor.persistent = true;
        actor.scripts.emplace_back().behaviour = "StructureSceneCommands";
        layout.objects.push_back(actor);
        SceneRuntime::SceneEnvironment environment;
        std::string error;
        Require(environment.Initialize(renderer, root, layout, error) && environment.StartScripts(error) &&
                    environment.Update(1.0 / 60, true, true),
                error.c_str());
        const auto& results = environment.SceneCommandResults();
        Require(results.size() == 3 && !results[0].succeeded && !results[0].error.empty() && results[1].succeeded &&
                    results[2].succeeded && results[2].command.unload,
                "committed world frame exposes ordered per-command results including partial failure");
        Require(environment.Update(1.0 / 60, true, true) && *completed == 2 && *failed == 1,
                "command completion and failure reach the next script update once");
        ValidateFramePreparation(renderer, root);
        Require(renderer.WaitForIdle(), "scene command GPU completion");
        std::cout << "PASS: explicit GPU synchronization, per-command results and script completion events\n";
    }
}

int main(int argc, char** argv)
{
    try
    {
        const auto root = std::filesystem::absolute("generated/tests/architecture-structure") /
                          (std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
        std::filesystem::create_directories(root / "Assets/Scenes");
        ValidateDocumentOpen(root);
        ValidateResourceChanges();
        ValidateScriptCatalogs();
        if (argc > 1 && std::string_view(argv[1]) == "--gpu")
            ValidateSceneCommands(root);
        return 0;
    }
    catch (const std::exception& failure)
    {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
