#include "CameraPanel.h"
#include "ObjectPanel.h"
#include "ProjectPanel.h"
#include "BuildPanel.h"
#include "ScriptAuthoringPanel.h"
#include "LightmapPanel.h"
#include "ProfilerPanel.h"
#include <chrono>
#include "AssetChanges.h"
#include "ModelDrop.h"
#include "SceneSelection.h"
#include "TransformGizmo.h"
#include "IkSceneHandles.h"
#include "EditHistory.h"
#include "FocusSelection.h"
#include "PanelLayout.h"
#include "ScenePanel.h"
#include "ConsolePanel.h"
#include "SceneDocument.h"
#include "SceneSnapshotCache.h"
#include "SceneRecovery.h"
#include "PrefabEditPlan.h"
#include "ComponentEditResources.h"
#include "SaveAsPanel.h"
#include "GameSession.h"
#include "GamePointerCapture.h"
#include <SceneRuntime/Prefab.h>
#include "UiCanvasPanel.h"
#include "UiEditorPanel.h"
#include "AudioPreview.h"
#include "EditorFonts.h"
#include "ProjectSettingsPanel.h"
#include "GameProject.h"
#include <Engine/Graphics/Resources/RenderTexture.h>
#include <SceneRuntime/ScenePresentation.h>
#include <SceneRuntime/SceneView.h>
#include <SceneRuntime/SceneWorld.h>
#include <Engine/Core/Application.h>
#include <Engine/Core/Log.h>
#include <Engine/Core/DiagnosticPaths.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <imgui.h>
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace
{
    std::filesystem::path ContentRoot()
    {
        std::wstring executable(32768, L'\0');
        const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (!length || length >= executable.size())
            return {};
        executable.resize(length);
        auto directory = std::filesystem::path(executable).parent_path();
        // 開発時は元のContentを使い、将来の保存先もビルド出力のコピーにしません。
        for (auto parent = directory; !parent.empty(); parent = parent.parent_path())
        {
            if (std::filesystem::is_directory(parent / "Content/Assets/Fonts"))
                return parent / "Content";
            if (parent == parent.parent_path())
                break;
        }
        return directory; // 配布時はEditorに同梱されたContentを読みます。
    }

    std::filesystem::path DefaultGameRoot()
    {
        const auto current = std::filesystem::current_path();
        if (std::filesystem::is_regular_file(current / "WP1.project.json"))
            return Editor::GameProject::Open(current);
        const auto selected = Editor::GameProject::Choose();
        return selected.value_or(std::filesystem::path{});
    }

    class EditorApplication final
    {
    public:
        explicit EditorApplication(const std::filesystem::path& content, std::filesystem::path scene = {})
            : root(content)
        {
            if (scene.empty())
                scene = Engine::AssetDatabase::Path(SceneRuntime::ProjectSettings::Load(root).startupScene);
            if (scene.is_absolute() || scene.has_root_name() || scene.extension() != ".json" ||
                !Editor::ProjectCatalog::Text(scene).starts_with("Assets/Scenes/") ||
                std::any_of(scene.begin(), scene.end(),
                            [](const auto& part)
                            {
                                return part == "..";
                            }))
                throw std::runtime_error("Invalid initial scene path");
            document.OpenPath(root / scene);
        }
        int Run()
        {
            if (root.empty())
                return 1;
            camera.SetResetPose({0, 3, -10}, 0, 0);
            camera.GetCamera().SetPerspective(DirectX::XM_PIDIV4, 16.0f / 9.0f, 0.1f, 1000);
            camera.SetMoveSpeed(8.0f);
            projectPanel.Scan(root);
            std::string scriptError;
            if (!SceneRuntime::ScriptModule::Reload(root, scriptError))
                Engine::Log::Warning(scriptError);
            try
            {
                playerInputs.SetBindings(SceneRuntime::ProjectSettings::Load(root).inputActions);
            }
            catch (const std::exception& exception)
            {
                Engine::Log::Warning(exception.what());
            }
            Engine::ApplicationCallbacks callbacks;
            callbacks.closeRequested = [&]()
            {
                closeRequested = true;
            };
            callbacks.shouldClose = [&]()
            {
                if (closeConfirmed)
                    Editor::PanelLayout::Save();
                return closeConfirmed;
            };
            callbacks.update = [&](double dt, const Engine::Keyboard& input)
            {
                keyboard = &input;
                seconds = dt;
                if (!input.IsActive())
                {
                    cameraPanel.CancelDrag();
                    pointerCapture.Release();
                }
            };
            callbacks.draw = [&](Engine::DirectX12Renderer& renderer)
            {
                return Draw(renderer);
            };
            Engine::ApplicationSettings settings;
            settings.title = L"WP1 エディター";
            Engine::Application application;
            return application.Run(settings, callbacks);
        }

    private:
        void RefreshPlayerBindings()
        {
            if (projectSettingsPanel.TakeSaved())
            {
                try
                {
                    playerInputs.SetBindings(SceneRuntime::ProjectSettings::Load(root).inputActions);
                }
                catch (const std::exception& exception)
                {
                    Engine::Log::Warning(exception.what());
                }
            }
        }

        Engine::RenderResult Draw(Engine::DirectX12Renderer& renderer)
        {
            const auto updateStart = std::chrono::steady_clock::now();
            if (!PrepareFrameResources(renderer))
                return Engine::RenderResult::Failed;
            audioPreview.Process(root);
            RefreshPlayerBindings();
            UpdateInput();
            editingUiReady = presentation && presentation->Prepare(renderer, root, document.Layout(), fileStatus);
            if (presentation && !editingUiReady)
                LogResult(false);
            if (gameSession.Runtime() && !gameSession.Runtime()->Prepare(renderer, root, fileStatus))
                LogResult(false);
            UpdatePlayInput();
            UpdateGamePointer();
            gameSession.Update(seconds, keyboard && keyboard->IsActive() && !closeRequested);
            if (auto command = gameSession.TakeCommand())
                pendingUiEvent = std::move(command);
            ApplyUiEvent(renderer);
            if (gameSession.Runtime() && !gameSession.Runtime()->Prepare(renderer, root, fileStatus))
                LogResult(false);
            if (gameSession.State().IsEditing())
                recovery.Poll(RecoveryDirectory(), document.Path(), SceneJson(), sceneLoaded && editState.HasChanges(),
                              seconds);
            profilerPanel.Update(
                renderer, DisplayedWorld(), seconds,
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - updateStart).count());
            bool rendered = true;
            const auto result = renderer.Render(
                preview ? world.Layout().settings.background : std::array<float, 4>{0.10f, 0.11f, 0.13f, 1},
                [&](ID3D12GraphicsCommandList* commands, float aspect)
                {
                    static_cast<void>(aspect);
                    rendered = RenderViews(commands, renderer);
                },
                [&]()
                {
                    DrawUi();
                });
            return rendered ? result : Engine::RenderResult::Failed;
        }

        void UpdateInput()
        {
            gamepad.Update(keyboard && keyboard->IsActive() && gameSession.State().CanPause());
            if (keyboard)
            {
                playerSnapshot = Engine::InputActions::Capture(
                    *keyboard, &gamepad, gamePanel.Focused() && gamePanel.Hovered() && !ImGui::IsAnyItemActive());
                playerInputs.Update(playerSnapshot);
            }
        }

        bool CanRoutePlayInput()
        {
            return GameUiAcceptsInput() && gameSession.Runtime()->Ui().focused.empty();
        }
        void UpdatePlayInput()
        {
            if (gameSession.State().CanPause() && gameSession.Runtime() && keyboard && keyboard->IsActive() &&
                !closeRequested)
            {
                const bool input = CanRoutePlayInput();
                if (const auto event = gameSession.PrepareInput(playerInputs, input, seconds, &playerSnapshot))
                    pendingUiEvent = *event;
            }
        }

        bool PrepareFrameResources(Engine::DirectX12Renderer& renderer)
        {
            buildPanel.Poll(root);
            scriptPanel.Poll(root, gameSession.State().IsEditing());
            if (!ApplyPendingChanges(renderer) || !PrepareSceneTexture(renderer) || !PrepareGameTexture(renderer) ||
                !PrepareUiTexture(renderer))
                return false;
            if (gameSession.State().IsEditing() && !projectPanel.PreparePreview(renderer, root))
                return false;
            return true;
        }

        bool ApplyPrefabChanges(const Editor::ObjectRequest& request, std::string& createdId)
        {
            try
            {
                auto candidate = world.Layout();
                if (request.action == Editor::ObjectAction::AddPrefab)
                {
                    auto asset = SceneRuntime::SceneLayout::Load(root / request.model, root);
                    SceneRuntime::Prefab::Refresh(asset, root);
                    createdId = SceneRuntime::Prefab::Instantiate(candidate, asset, request.model, request.position);
                }
                else if (request.action == Editor::ObjectAction::RefreshPrefabs)
                    SceneRuntime::Prefab::Refresh(candidate, root);
                else if (request.action == Editor::ObjectAction::RevertPrefab)
                    SceneRuntime::Prefab::RevertInstance(candidate, request.id, root);
                else if (request.action == Editor::ObjectAction::RevertPrefabProperty)
                    SceneRuntime::Prefab::RevertOverride(candidate, request.id, request.property);
                else if (request.action == Editor::ObjectAction::BindPrefabReferences)
                    SceneRuntime::Prefab::BindReferences(candidate, request.id, request.prefabBindings);
                else if (request.action == Editor::ObjectAction::ApplyPrefabProperty)
                    return CommitPrefabPlan(
                        Editor::PrefabEdits::Override(world.Layout(), request.id, request.property, root));
                else
                    SceneRuntime::Prefab::Unpack(candidate, request.id);
                return world.ReplaceLayout(std::move(candidate), root, fileStatus);
            }
            catch (const std::exception& exception)
            {
                fileStatus = exception.what();
                return false;
            }
        }

        bool CommitPrefabPlan(Editor::PrefabEditPlan plan)
        {
            const auto before = Snapshot(SceneJson());
            if (!Editor::FileTransaction::Apply(plan.files, true, fileStatus,
                                                [&]()
                                                {
                                                    return world.ReplaceLayout(std::move(plan.scene), root, fileStatus);
                                                }))
                return false;
            history.RecordTransaction(before, Snapshot(SceneJson()), std::move(plan.files));
            return true;
        }
        bool SavePrefabChanges(const Editor::ObjectRequest& request)
        {
            try
            {
                auto plan = Editor::PrefabEdits::Save(world.Layout(), request.id, request.model, root,
                                                      request.action == Editor::ObjectAction::ApplyPrefab,
                                                      request.action == Editor::ObjectAction::SavePrefabVariant);
                if (!CommitPrefabPlan(std::move(plan)))
                    return false;
                projectPanel.Scan(root);
                fileStatus = "Prefabを保存しました。";
                return true;
            }
            catch (const std::exception& exception)
            {
                fileStatus = exception.what();
                return false;
            }
        }

        static bool IsPrefabLayoutAction(Editor::ObjectAction action)
        {
            switch (action)
            {
            case Editor::ObjectAction::AddPrefab:
            case Editor::ObjectAction::RefreshPrefabs:
            case Editor::ObjectAction::UnpackPrefab:
            case Editor::ObjectAction::RevertPrefab:
            case Editor::ObjectAction::RevertPrefabProperty:
            case Editor::ObjectAction::ApplyPrefabProperty:
            case Editor::ObjectAction::BindPrefabReferences:
                return true;
            default:
                return false;
            }
        }

        static bool IsPrefabSaveAction(Editor::ObjectAction action)
        {
            switch (action)
            {
            case Editor::ObjectAction::SavePrefab:
            case Editor::ObjectAction::ApplyPrefab:
            case Editor::ObjectAction::SavePrefabVariant:
                return true;
            default:
                return false;
            }
        }

        void SetObjectResultStatus(Editor::ObjectAction action)
        {
            if (action != Editor::ObjectAction::SavePrefab && action != Editor::ObjectAction::ApplyPrefab &&
                action != Editor::ObjectAction::SavePrefabVariant)
                fileStatus = action == Editor::ObjectAction::BindPrefabReferences ? "Prefabの接続を更新しました。"
                             : action == Editor::ObjectAction::RevertPrefab ||
                                     action == Editor::ObjectAction::RevertPrefabProperty ||
                                     action == Editor::ObjectAction::ApplyPrefabProperty
                                 ? "Prefabを更新しました。"
                             : action == Editor::ObjectAction::Delete     ? "削除しました。"
                             : action == Editor::ObjectAction::Duplicate  ? "複製しました。"
                             : action == Editor::ObjectAction::Components ? "コンポーネントを更新しました。"
                             : action == Editor::ObjectAction::Settings   ? "シーン設定を更新しました。"
                                                                          : "追加しました。";
        }
        static bool RefreshViewport(bool valid, bool changed, bool realtime, bool focused)
        {
            return valid && (changed || realtime || focused);
        }

        bool RenderViews(ID3D12GraphicsCommandList* commands, const Engine::DirectX12Renderer& renderer)
        {
            if (preview)
            {
                if (presentation)
                    presentation->Draw(commands, world, renderer.GetWidth(), renderer.GetHeight(),
                                       {nullptr, 0, true, nullptr, editingUiReady});
                return true;
            }
            bool success = projectPanel.RenderPreview(commands);
            const auto signature =
                std::to_string(world.RenderVersion()) + "," +
                std::to_string(reinterpret_cast<uintptr_t>(gameSession.Runtime())) + "," +
                std::to_string(sceneTexture.GetWidth()) + "," + std::to_string(sceneTexture.GetHeight()) + "," +
                std::to_string(gameTexture.GetWidth()) + "," + std::to_string(gameTexture.GetHeight());
            const bool refresh = signature != viewSignature;
            viewSignature = signature;
            if (RefreshViewport(sceneViewport.Valid(), refresh, DisplayedWorld().Layout().settings.sceneRealtime,
                                scenePanel.Focused()))
                success = DrawSceneTexture(commands) && success;
            if (RefreshViewport(gamePanel.Viewport().Valid(), refresh, DisplayedWorld().Layout().settings.gameRealtime,
                                gamePanel.Focused()))
                success = DrawGameTexture(commands) && success;
            if (uiEditorPanel.Visible() && presentation && editingUiReady)
            {
                if (!uiTexture.Begin(commands, {.12f, .12f, .14f, 1}))
                    return false;
                presentation->DrawUi(commands, world.Layout(), uiTexture.GetWidth(), uiTexture.GetHeight(),
                                     uiEditorPanel.PreviewState());
                success = uiTexture.End(commands) && success;
            }
            return success;
        }

        void ReportStatus(std::string message, bool success)
        {
            if (message == fileStatus)
                return;
            fileStatus = std::move(message);
            LogResult(success);
        }

        void LogResult(bool success) const
        {
            if (!fileStatus.empty())
                Engine::Log::Write(success ? Engine::LogLevel::Info : Engine::LogLevel::Error, fileStatus);
        }

        bool PrepareSceneTexture(Engine::DirectX12Renderer& renderer)
        {
            if (!requestedSceneSize[0] || !requestedSceneSize[1])
                return true;
            const auto scale = DisplayedWorld().Layout().settings.sceneRenderScale;
            const unsigned int width = std::max(1U, static_cast<unsigned int>(requestedSceneSize[0] * scale)),
                               height = std::max(1U, static_cast<unsigned int>(requestedSceneSize[1] * scale));
            if (sceneTexture.GetWidth() == width && sceneTexture.GetHeight() == height)
                return true;
            if (!sceneTexture.Resize(renderer, width, height))
            {
                ReportStatus("シーンの描画テクスチャをリサイズできませんでした。", false);
                return sceneTexture.GetResource() != nullptr;
            }
            sceneTextureId = renderer.SetSceneTexture(sceneTexture.GetShaderResourceView()).ptr;
            return sceneTextureId != 0;
        }

        bool PrepareGameTexture(Engine::DirectX12Renderer& renderer)
        {
            if (!requestedGameSize[0] || !requestedGameSize[1])
                return true;
            const auto scale = DisplayedWorld().Layout().settings.gameRenderScale;
            const unsigned int width = std::max(1U, static_cast<unsigned int>(requestedGameSize[0] * scale)),
                               height = std::max(1U, static_cast<unsigned int>(requestedGameSize[1] * scale));
            if (gameTexture.GetWidth() == width && gameTexture.GetHeight() == height)
                return true;
            if (!gameTexture.Resize(renderer, width, height))
            {
                ReportStatus("ゲームの描画テクスチャをリサイズできませんでした。", false);
                return gameTexture.GetResource() != nullptr;
            }
            gameTextureId = renderer.SetSceneTexture(gameTexture.GetShaderResourceView(), 1).ptr;
            return gameTextureId != 0;
        }
        bool PrepareUiTexture(Engine::DirectX12Renderer& renderer)
        {
            const auto size = uiEditorPanel.Resolution();
            if (uiTexture.GetWidth() == size[0] && uiTexture.GetHeight() == size[1])
                return true;
            if (!uiTexture.Resize(renderer, size[0], size[1]))
                return false;
            uiTextureId = renderer.SetSceneTexture(uiTexture.GetShaderResourceView(), 4).ptr;
            return uiTextureId != 0;
        }

        bool DrawGameTexture(ID3D12GraphicsCommandList* commands)
        {
            if (!gameTexture.Begin(commands, world.Layout().settings.background))
                return false;
            if (gameSession.Runtime())
                gameSession.Draw(commands, gameTexture.GetWidth(), gameTexture.GetHeight());
            else if (presentation)
                presentation->Draw(commands, world, gameTexture.GetWidth(), gameTexture.GetHeight(),
                                   {nullptr, 0, true, nullptr, editingUiReady, editingUiReady});
            return gameTexture.End(commands);
        }

        const SceneRuntime::SceneWorld& DisplayedWorld() const
        {
            const auto* runtime = gameSession.Runtime();
            return runtime ? runtime->World() : world;
        }

        bool DrawSceneTexture(ID3D12GraphicsCommandList* commands)
        {
            if (!sceneTexture.Begin(commands, world.Layout().settings.background))
                return false;
            const auto* runtime = gameSession.Runtime();
            if (runtime)
                runtime->Draw(commands, sceneTexture.GetWidth(), sceneTexture.GetHeight(), &camera.GetCamera(),
                              showSceneUi);
            else if (presentation)
                presentation->Draw(commands, world, sceneTexture.GetWidth(), sceneTexture.GetHeight(),
                                   {&camera.GetCamera(), 0, true, nullptr, showSceneUi && editingUiReady});
            return sceneTexture.End(commands);
        }

        bool ApplyReload(Engine::DirectX12Renderer& renderer)
        {
            if (reloadRequested)
            {
                reloadRequested = false;
                if (!renderer.WaitForIdle())
                    return false;
                if (world.Reload(root, document.Path(), fileStatus, true))
                {
                    document.OpenPath(document.Path());
                    sceneLoaded = true;
                    editState.Reloaded();
                    history.Reset(Snapshot(SceneJson()));
                    fileStatus = "再読み込みしました。";
                    LogResult(true);
                }
                else
                    LogResult(false);
            }
            return true;
        }

        bool ApplySceneChange(Engine::DirectX12Renderer& renderer)
        {
            if (!document.Ready())
                return true;
            if (!renderer.WaitForIdle())
                return false;
            if (document.Apply(world, root, fileStatus))
            {
                sceneLoaded = true;
                editState.Reloaded();
                history.Reset({SceneJson(), {}});
                editState.SetChanged(document.UnsavedNew());
                cameraPanel.CancelDrag();
                fileStatus = document.UnsavedNew() ? "新規シーンを作成しました（未保存）。" : "シーンを開きました。";
                LogResult(true);
            }
            else
                LogResult(false);
            return true;
        }

        Editor::EditHistory::State Snapshot(const std::string& json) const
        {
            return {json, editState.SelectedId(), editState.SelectedIds()};
        }
        const std::string& SceneJson()
        {
            return sceneSnapshots.Get(document.Layout());
        }
        std::filesystem::path RecoveryDirectory() const
        {
            const auto diagnostics = Engine::GetDiagnosticsRoot();
            return (diagnostics.empty() ? root.parent_path() / "generated" : diagnostics) / "Editor/Recovery";
        }
        bool ApplyRecovery(Engine::DirectX12Renderer& renderer)
        {
            if (!pendingRecovery)
                return true;
            if (!gameSession.State().IsEditing())
            {
                pendingRecovery = false;
                ReportStatus("復旧は再生を停止してから行ってください。", false);
                return true;
            }
            if (!renderer.WaitForIdle())
                return false;
            pendingRecovery = false;
            try
            {
                auto layout = Editor::SceneRecovery::Read(RecoveryDirectory(), document.Path());
                if (!world.ReplaceLayout(std::move(layout), root, fileStatus))
                {
                    LogResult(false);
                    return true;
                }
                sceneLoaded = true;
                editState.Select({});
                history.Observe(Snapshot(SceneJson()), {});
                editState.SetChanged(true);
                recovery.Clear(RecoveryDirectory(), document.Path());
                ReportStatus("復旧用シーンを読み込みました。内容を確認して保存してください。", true);
            }
            catch (const std::exception& e)
            {
                ReportStatus(e.what(), false);
            }
            return true;
        }

        bool ApplyHistory(Engine::DirectX12Renderer& renderer)
        {
            if (pendingHistory)
            {
                if (!renderer.WaitForIdle())
                    return false;
                const bool redo = *pendingHistory;
                pendingHistory.reset();
                const auto target = history.Target(redo);
                if (Editor::FileTransaction::Apply(
                        history.TargetFiles(redo), redo, fileStatus,
                        [&]()
                        {
                            return world.ReplaceLayout(SceneRuntime::SceneLayout::Parse(target.json), root, fileStatus);
                        }))
                {
                    history.Applied(redo);
                    projectPanel.Scan(root);
                    editState.RestoreSelection(target.selections, target.selection);
                    editState.SetChanged(document.UnsavedNew() || history.Dirty(target.json));
                    fileStatus = redo ? "やり直しました。" : "元に戻しました。";
                    LogResult(true);
                }
                else
                    LogResult(false);
            }
            return true;
        }

        bool ApplyObjectRequest(const Editor::ObjectRequest& request, std::string& createdId)
        {
            if (request.action == Editor::ObjectAction::Add || request.action == Editor::ObjectAction::AddEmpty)
            {
                SceneRuntime::ScenePlacement placement;
                if (request.action == Editor::ObjectAction::Add)
                {
                    placement.SetModel(request.model);
                    placement.scale = {4, 4, 4};
                }
                placement.position = request.position;
                return world.AddObject(std::move(placement), root, createdId, fileStatus);
            }
            if (IsPrefabLayoutAction(request.action))
                return ApplyPrefabChanges(request, createdId);
            if (IsPrefabSaveAction(request.action))
                return SavePrefabChanges(request);
            if (request.action == Editor::ObjectAction::Duplicate)
            {
                return editState.DuplicateObjects(world, request.ids, {4, 0, 0}, fileStatus);
            }
            if (request.action == Editor::ObjectAction::GenerateRagdoll)
                return world.GenerateRagdoll(request.id, root, fileStatus);
            if (request.action == Editor::ObjectAction::Delete)
                return editState.DeleteObjects(world, request.ids, fileStatus);
            if (request.action == Editor::ObjectAction::Settings)
                return request.settings && world.SetSettings(*request.settings, fileStatus);
            if (!request.componentBatch.empty())
                return world.SetComponentBatch(request.componentBatch, root, fileStatus);
            return request.components && world.SetComponents(request.id, *request.components, root, fileStatus);
        }

        bool ApplyObject(Engine::DirectX12Renderer& renderer)
        {
            if (!pendingObject)
                return true;
            if (Editor::ComponentEditResources::NeedsIdle(world.Layout(), *pendingObject) && !renderer.WaitForIdle())
                return false;
            auto request = std::move(*pendingObject);
            pendingObject.reset();
            if (request.interaction.empty())
                history.Commit();
            std::string createdId;
            const bool success = ApplyObjectRequest(request, createdId);
            if (success)
            {
                if (!createdId.empty())
                    editState.ObjectChanged(createdId);
                SetObjectResultStatus(request.action);
                history.Observe(Snapshot(SceneJson()), request.interaction);
                editState.SetChanged(document.UnsavedNew() || history.Dirty(SceneJson()));
            }
            LogResult(success);
            return true;
        }

        bool ApplyPendingChanges(Engine::DirectX12Renderer& renderer)
        {
            if (!initialized)
            {
                if (!Editor::EditorFonts::Initialize(ContentRoot(), fileStatus))
                {
                    LogResult(false);
                    return false;
                }
                Editor::Language::Initialize();
                const auto settingsRoot = Engine::GetDiagnosticsRoot();
                Editor::PanelLayout::Initialize(settingsRoot.empty() ? std::filesystem::path{}
                                                                     : settingsRoot / "Editor/layout.ini");
                sceneLoaded =
                    world.Initialize(renderer, root, document.Path(), root / "Shaders/Mesh.hlsl", &fileStatus, true);
                presentation = std::make_unique<SceneRuntime::ScenePresentation>();
                if (!presentation->Initialize(renderer, root, fileStatus, context))
                {
                    presentation.reset();
                    LogResult(false);
                }
                initialized = true;
                if (sceneLoaded)
                    history.Reset(Snapshot(SceneJson()));
                Engine::Log::Write(sceneLoaded ? Engine::LogLevel::Info : Engine::LogLevel::Error,
                                   sceneLoaded ? "エディターのシーンを読み込みました。" : fileStatus);
            }
            return ApplySceneChange(renderer) && ApplyReload(renderer) && ApplyHistory(renderer) &&
                   ApplyObject(renderer) && ApplyPlay(renderer) && ApplySceneCollection(renderer) &&
                   ApplyAssets(renderer) && ApplyRecovery(renderer);
        }

        bool ApplySceneCollection(Engine::DirectX12Renderer& renderer)
        {
            if (!pendingSceneCollection)
                return true;
            const auto [name, unload] = std::move(*pendingSceneCollection);
            pendingSceneCollection.reset();
            if (!renderer.WaitForIdle())
                return false;
            if (auto* runtime = gameSession.Runtime())
            {
                const bool success =
                    unload ? runtime->UnloadScene(name, fileStatus) : runtime->LoadScene(name, true, fileStatus);
                LogResult(success);
                return true;
            }
            try
            {
                SceneRuntime::SceneCollection collection;
                collection.Reset(world.Layout(), document.Path().lexically_relative(root).generic_string());
                auto layout =
                    unload ? collection.Unload(world.Layout(), name)
                           : collection.Add(world.Layout(), SceneRuntime::SceneLayout::Load(root / name, root), name);
                const auto previousUi =
                    presentation ? std::optional<SceneRuntime::SceneUi>{presentation->CaptureUi()} : std::nullopt;
                if (presentation && !presentation->Prepare(renderer, root, layout, fileStatus))
                {
                    editingUiReady = false;
                    LogResult(false);
                }
                if (!world.ReplaceLayout(std::move(layout), root, fileStatus))
                {
                    if (presentation && previousUi)
                        presentation->RestoreUi(*previousUi);
                    LogResult(false);
                    return true;
                }
                history.Observe(Snapshot(SceneJson()), {});
                editState.SetChanged(true);
                fileStatus = "シーン構成を更新しました。保存すると合成シーンと所属を保存します。";
                LogResult(true);
            }
            catch (const std::exception& exception)
            {
                ReportStatus(exception.what(), false);
            }
            return true;
        }

        bool CanReloadAssets() const
        {
            return sceneLoaded && gameSession.State().IsEditing() && !pendingPlay && !document.Pending() &&
                   !pendingObject && !pendingHistory && !gizmo.IsDragging() && editState.Interaction().empty() &&
                   !ikHandles.IsDragging() && !uiCanvasPanel.IsDragging();
        }

        bool ApplyAssets(Engine::DirectX12Renderer& renderer)
        {
            assetChanges.Poll(root, seconds);
            assetReloadRequested = projectPanel.TakeAssetReloadRequest() || assetReloadRequested;
            projectPanel.SetReloadPending(assetReloadRequested || assetChanges.Pending(), assetChanges.Error());
            if (!CanReloadAssets())
                return true;
            const bool changed = assetChanges.TakeReady(true);
            if (!assetReloadRequested && !changed)
                return true;
            assetReloadRequested = false;
            if (!renderer.WaitForIdle())
                return false;
            auto candidate = std::make_unique<SceneRuntime::ScenePresentation>();
            auto resolved = world.Layout();
            try
            {
                resolved.ResolveAssets(Engine::AssetDatabase(context, true));
            }
            catch (const std::exception& exception)
            {
                fileStatus = exception.what();
                LogResult(false);
                return true;
            }
            const bool success = Editor::ValidateProjectShaders(root, fileStatus) &&
                                 candidate->Initialize(renderer, root, fileStatus, context) &&
                                 candidate->Prepare(renderer, root, resolved, fileStatus) &&
                                 world.ReloadAssets(renderer, root, root / "Shaders/Mesh.hlsl", fileStatus);
            if (success)
            {
                presentation = std::move(candidate);
                projectPanel.Scan(root);
                fileStatus = "アセットを再読み込みしました。未保存のシーンと履歴を維持しています。";
            }
            LogResult(success);
            return true;
        }

        bool ApplyPlay(Engine::DirectX12Renderer& renderer)
        {
            if (!pendingPlay)
                return true;
            const auto command = *pendingPlay;
            pendingPlay.reset();
            if (command == Editor::GameSession::Command::Play && !scriptPanel.Ready())
            {
                ReportStatus("C++ゲーム処理のコンパイル・再読み込みを完了してから再生してください。", false);
                return true;
            }
            if (command == Editor::GameSession::Command::Pause)
            {
                if (gameSession.Pause())
                    ReportStatus("ゲームを一時停止しました。", true);
                return true;
            }
            if (command == Editor::GameSession::Command::Step)
            {
                if (gameSession.Step())
                    ReportStatus("1フレーム（1/60秒）進めました。", true);
                return true;
            }
            if (!renderer.WaitForIdle())
                return false;
            if (command == Editor::GameSession::Command::Stop)
                StopGame();
            else
                StartGame(renderer);
            return true;
        }

        void StartGame(const Engine::DirectX12Renderer& renderer)
        {
            const bool starting = gameSession.State().IsEditing();
            Editor::AudioPreview::StopRequest();
            audioPreview.Process(root);
            if (projectSettingsPanel.TakeSaved())
            {
                try
                {
                    playerInputs.SetBindings(SceneRuntime::ProjectSettings::Load(root).inputActions);
                }
                catch (const std::exception& exception)
                {
                    Engine::Log::Warning(exception.what());
                }
            }
            gamepad.Update(keyboard && keyboard->IsActive() && gameSession.State().CanPause());
            if (keyboard)
                playerInputs.Update(Engine::InputActions::Capture(*keyboard, &gamepad));
            if (!gameSession.Play(renderer, root, document.Layout(), fileStatus, {}, context))
            {
                LogResult(false);
                return;
            }
            if (starting)
            {
                history.Commit();
                runtimeEditState.Reloaded();
                runtimeEditState.RestoreSelection(editState.SelectedIds(), editState.SelectedId());
            }
            focusGame = true;
            cameraPanel.CancelDrag();
            ReportStatus("再生を開始しました。", true);
        }

        void StopGame()
        {
            if (!gameSession.State().CanStop())
                return;
            if (gameSession.Stop())
            {
                runtimeEditState.Reloaded();
                ReportStatus("編集へ戻りました。", true);
            }
        }

        bool Save()
        {
            if (!sceneLoaded)
            {
                fileStatus = "保存するシーンが読み込まれていません。";
                LogResult(false);
                return false;
            }
            try
            {
                std::vector<Editor::DocumentSavePlan> plans;
                plans.push_back(document.PrepareSave(document.Layout()));
                projectPanel.PrepareMaterialSaves(plans);
                Editor::DocumentSave::Apply(std::move(plans));
                SavedSuccessfully("保存しました。");
                return true;
            }
            catch (const std::exception& error)
            {
                fileStatus = std::string("保存に失敗しました：") + error.what();
                LogResult(false);
                return false;
            }
        }

        void SavedSuccessfully(std::string status)
        {
            recovery.Clear(RecoveryDirectory(), document.Path());
            history.Saved(SceneJson());
            editState.MarkSaved();
            projectPanel.Scan(root);
            fileStatus = std::move(status);
            LogResult(true);
        }

        bool SaveAs(const std::filesystem::path& target, bool overwrite)
        {
            if (!sceneLoaded)
                return false;
            try
            {
                std::vector<Editor::DocumentSavePlan> plans;
                plans.push_back(document.PrepareSaveAs(document.Layout(), target, overwrite));
                projectPanel.PrepareMaterialSaves(plans);
                Editor::DocumentSave::Apply(std::move(plans));
                SavedSuccessfully("別名で保存しました：" + Editor::ProjectCatalog::Text(document.Path().filename()));
                return true;
            }
            catch (const std::exception& error)
            {
                fileStatus = std::string("別名保存に失敗しました：") + error.what();
                LogResult(false);
                return false;
            }
        }

        void DrawUi()
        {
            if (preview || closeRequested)
                pointerCapture.Release();
            editState.BeginFrame();
            if (!preview)
            {
                DrawMenuBar();
                DrawToolbar();
            }
            Editor::PanelLayout::BeginFrame(preview);
            if (!preview)
            {
                consolePanel.Draw();
                buildPanel.Draw(root, !editState.HasChanges() && !document.UnsavedNew() &&
                                          !projectPanel.HasMaterialChanges());
                scriptPanel.Draw(root);
                profilerPanel.Draw(root);
                DrawRecovery();
            }
            Editor::TransformGizmo::BeginFrame();
            sceneViewport = {};
            if (preview)
            {
                if (closeRequested)
                    DrawClosePopup();
                else
                    DrawPreview();
                return;
            }
            DrawGamePanel();
            uiEditorPanel.Draw(uiTextureId, world.Layout(), editState, SceneEditingEnabled());
            scenePanel.Begin(sceneTextureId);
            sceneViewport = scenePanel.Viewport();
            requestedSceneSize = scenePanel.RequestedSize();
            AcceptModelDrop();
            if (closeRequested)
            {
                DrawClosePopup();
                Editor::ScenePanel::End();
                return;
            }
            UpdateCamera();
            const bool canFocus = CanFocus();
            UpdateGizmoShortcuts(canFocus);
            FocusSelection(canFocus);
            UpdateObjects();
            const bool historyEnabled = HistoryEnabled();
            UpdateShortcuts(historyEnabled);
            DrawCommands();
            if (!preview)
                Editor::SceneSelection::Draw(DisplayedWorld(), camera.GetCamera(),
                                             gameSession.Runtime() ? runtimeEditState : editState, sceneViewport);
            Editor::ScenePanel::End();
        }

        void ApplyUiEvent(Engine::DirectX12Renderer& renderer)
        {
            if (gameSession.Runtime())
                gameSession.Runtime()->FlushSceneChanges();
            if (!pendingUiEvent)
                return;
            if (!renderer.WaitForIdle())
                return;
            const auto event = std::move(*pendingUiEvent);
            pendingUiEvent.reset();
            if (event.action == "quit")
            {
                StopGame();
                return;
            }
            if (event.action == "loadScene")
                runtimeEditState.Reloaded();
        }

        void ApplyGamePointer(SceneRuntime::SceneEnvironment& runtime, const std::vector<char32_t>& characters,
                              const Engine::DiagnosticPointer* diagnostic, float wheel)
        {
            const auto& v = gamePanel.Viewport();
            POINT cursor{};
            if (diagnostic)
            {
                cursor.x = diagnostic->x;
                cursor.y = diagnostic->y;
            }
            else
            {
                GetCursorPos(&cursor);
                ScreenToClient(keyboard->WindowHandle(), &cursor);
            }
            const ImVec2 mouse{static_cast<float>(cursor.x), static_cast<float>(cursor.y)};
            UpdateGameShortcut(runtime);
            const bool inside = v.Contains(mouse.x, mouse.y) && gamePanel.Hovered();
            const auto event = gameSession.Playback().Pointer(
                runtime, gameTexture.GetWidth(), gameTexture.GetHeight(),
                {inside ? (mouse.x - v.x) * gameTexture.GetWidth() / v.width : -100000,
                 inside ? (mouse.y - v.y) * gameTexture.GetHeight() / v.height : -100000,
                 (playerSnapshot.mouseButtons & 1) != 0, false, false, inside ? wheel : 0},
                true);
            if (!event.object.empty())
                pendingUiEvent = event;
            if (GameUiAcceptsInput())
            {
                for (const auto character : characters)
                    runtime.UiTextInput(character);
            }
        }

        void UpdateGamePointer()
        {
            const auto characters =
                keyboard ? Engine::Window::ConsumeTextInput(keyboard->WindowHandle()) : std::vector<char32_t>{};
            const auto* diagnostic = keyboard ? keyboard->GetDiagnosticPointer() : nullptr;
            const float wheel = diagnostic ? diagnostic->wheel
                                : keyboard ? Engine::Window::ConsumeMouseWheel(keyboard->WindowHandle())
                                           : 0;
            auto* runtime = gameSession.Runtime();
            if (!runtime)
                return;
            if (!gameSession.State().CanPause() || !keyboard || !keyboard->IsActive())
            {
                gameSession.Playback().Pointer(*runtime, gameTexture.GetWidth(), gameTexture.GetHeight(), {}, false);
                return;
            }
            const auto& v = gamePanel.Viewport();
            if (!v.Valid())
                return;
            ApplyGamePointer(*runtime, characters, diagnostic, wheel);
        }

        void AcceptModelDrop()
        {
            if (closeRequested || !SceneEditingEnabled() || gizmo.IsDragging() || ikHandles.IsDragging() ||
                uiCanvasPanel.IsDragging() || ImGui::IsMouseDown(ImGuiMouseButton_Right) || !sceneViewport.Valid())
                return;
            if (!ImGui::BeginDragDropTarget())
                return;
            if (const auto* payload = ImGui::AcceptDragDropPayload(Editor::ModelPayload))
            {
                const auto* path = static_cast<const char*>(payload->Data);
                if (payload->DataSize > 1 && path[payload->DataSize - 1] == '\0' &&
                    std::char_traits<char>::length(path) == static_cast<size_t>(payload->DataSize - 1))
                {
                    camera.GetCamera().SetAspectRatio(sceneViewport.Aspect());
                    const auto mouse = ImGui::GetIO().MousePos;
                    if (const auto position =
                            Editor::ModelDropPosition(camera.GetCamera(), sceneViewport, mouse.x, mouse.y))
                        projectPanel.RequestDrop(editState, path, *position);
                }
            }
            ImGui::EndDragDropTarget();
        }

        void DrawRecovery()
        {
            if (!recovery.PreviousSession() && recovery.Error().empty())
                return;
            if (ImGui::Begin("シーンの復旧###Scene recovery"))
            {
                if (!recovery.Error().empty())
                    ImGui::TextWrapped("%s", recovery.Error().c_str());
                if (recovery.Available())
                {
                    ImGui::TextUnformatted("このシーンの復旧用データがあります。保存元のファイルは変更していません。");
                    ImGui::BeginDisabled(!gameSession.State().IsEditing() || document.Pending() || pendingRecovery);
                    if (ImGui::Button("復旧用データを読み込む###Restore recovery"))
                    {
                        if (editState.HasChanges())
                            ImGui::OpenPopup("現在の変更を置き換えますか###Confirm recovery");
                        else
                            pendingRecovery = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("復旧用データを破棄###Discard recovery"))
                        recovery.Clear(RecoveryDirectory(), document.Path());
                    ImGui::EndDisabled();
                }
                if (ImGui::BeginPopupModal("現在の変更を置き換えますか###Confirm recovery", nullptr,
                                           ImGuiWindowFlags_AlwaysAutoResize))
                {
                    ImGui::TextUnformatted("現在の未保存のシーンを復旧用データで置き換えます。Undoで戻せます。");
                    if (ImGui::Button("置き換える###Replace from recovery"))
                    {
                        pendingRecovery = true;
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("キャンセル###Cancel recovery"))
                        ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
            }
            ImGui::End();
        }

        void DrawClosePopup()
        {
            ikHandles.Cancel();
            cameraPanel.CancelDrag();
            if ((!sceneLoaded || !editState.HasChanges()) && !projectPanel.HasMaterialChanges())
            {
                closeConfirmed = true;
                return;
            }
            history.Observe(Snapshot(SceneJson()), {});
            // 編集中のギズモを止め、確認中は配置を変更しません。
            gizmo.UpdateAndDraw(world, camera.GetCamera(), editState, sceneViewport, false);
            if (!ImGui::IsPopupOpen("未保存の変更があります：終了###Exit with unsaved changes?"))
                ImGui::OpenPopup("未保存の変更があります：終了###Exit with unsaved changes?");
            if (ImGui::BeginPopupModal("未保存の変更があります：終了###Exit with unsaved changes?", nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextUnformatted("現在のシーンに未保存の変更があります。");
                if (ImGui::Button("保存して終了###Save and exit"))
                {
                    if (Save())
                    {
                        closeConfirmed = true;
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("保存せず終了###Exit without saving"))
                {
                    recovery.Clear(RecoveryDirectory(), document.Path());
                    closeConfirmed = true;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("キャンセル###Cancel"))
                {
                    closeRequested = false;
                    ImGui::CloseCurrentPopup();
                }
                if (!audioPreview.error.empty())
                    ImGui::TextWrapped("音声：%s", audioPreview.error.c_str());
                for (const auto& diagnostic : world.ResourceDiagnostics())
                    ImGui::TextWrapped("素材：%s", diagnostic.c_str());
                if (!fileStatus.empty())
                    ImGui::TextWrapped("%s", fileStatus.c_str());
                ImGui::EndPopup();
            }
        }

        void DrawPreview()
        {
            if (keyboard && keyboard->IsActive() && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                preview = false;
            ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.8f);
            if (ImGui::Begin("ゲーム画面の確認###Game composition", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextUnformatted("シーンのカメラ・照明：未保存の編集を含む現在の配置");
                ImGui::TextUnformatted("シーンのカメラ・空・照明・パーティクル：現在の編集中の配置");
                if (ImGui::Button("編集へ戻る（Escape）###Back to editing (Escape)"))
                    preview = false;
            }
            ImGui::End();
        }

        void UpdateCamera()
        {
            if (keyboard)
                cameraPanel.Draw(camera, *keyboard, seconds, sceneViewport, scenePanel.Hovered(),
                                 !gizmo.IsDragging() && !ikHandles.IsDragging() && !uiCanvasPanel.IsDragging() &&
                                     !ImGui::GetDragDropPayload());
            if (sceneViewport.Valid())
                camera.GetCamera().SetAspectRatio(sceneViewport.Aspect());
        }

        bool CanFocus() const
        {
            return editState.InspectedAsset().empty() && !pendingPlay && sceneViewport.Valid() && !document.Pending() &&
                   sceneLoaded && keyboard && keyboard->IsActive() && !gizmo.IsDragging() && !ikHandles.IsDragging() &&
                   !pendingObject && !pendingHistory && !reloadRequested && !uiCanvasPanel.IsDragging() &&
                   !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) &&
                   !ImGui::IsMouseDown(ImGuiMouseButton_Right);
        }

        void UpdateGizmoShortcuts(bool canFocus)
        {
            if (canFocus && !ImGui::IsAnyItemActive() && !ImGui::GetIO().WantTextInput && !ImGui::GetIO().KeyCtrl &&
                !ImGui::GetIO().KeyAlt)
            {
                if (ImGui::IsKeyPressed(ImGuiKey_1, false))
                    gizmo.SetMode(Editor::TransformGizmo::Mode::Move);
                if (ImGui::IsKeyPressed(ImGuiKey_2, false))
                    gizmo.SetMode(Editor::TransformGizmo::Mode::Rotate);
                if (ImGui::IsKeyPressed(ImGuiKey_3, false))
                    gizmo.SetMode(Editor::TransformGizmo::Mode::Scale);
            }
        }

        void FocusSelection(bool canFocus)
        {
            if (focusRequested ||
                (canFocus && !ImGui::IsAnyItemActive() && !ImGui::GetIO().WantTextInput && !ImGui::GetIO().KeyCtrl &&
                 !ImGui::GetIO().KeyAlt && ImGui::IsKeyPressed(ImGuiKey_F, false)))
            {
                focusRequested = false;
                if (canFocus && !editState.SelectedIds().empty())
                {
                    if (const auto position = Editor::FocusPosition(
                            DisplayedWorld(),
                            gameSession.Runtime() ? runtimeEditState.SelectedIds() : editState.SelectedIds(),
                            camera.GetCamera()))
                    {
                        cameraPanel.CancelDrag();
                        camera.GetCamera().SetPosition(*position);
                    }
                    else
                        fileStatus = "選択対象がカメラの範囲に収まらないか、対象のオブジェクトがありません。";
                }
            }
        }

        bool EditWidgetsEnabled() const
        {
            return !pendingPlay && gameSession.State().IsEditing() && sceneLoaded && !document.Pending() &&
                   !reloadRequested && !gizmo.IsDragging() && ImGui::GetTopMostPopupModal() == nullptr &&
                   !ikHandles.IsDragging() && !uiCanvasPanel.IsDragging();
        }

        bool SceneEditingEnabled() const
        {
            return !pendingPlay && gameSession.State().IsEditing() && sceneLoaded && !document.Pending() &&
                   !reloadRequested && keyboard && keyboard->IsActive() && !pendingObject && !pendingHistory &&
                   !reloadConfirmRequested && ImGui::GetTopMostPopupModal() == nullptr;
        }

        void UpdateObjects()
        {
            UpdateSceneTools();
            DirectX::XMFLOAT4X4 viewInverse;
            DirectX::XMStoreFloat4x4(&viewInverse, DirectX::XMMatrixInverse(nullptr, camera.GetViewMatrix()));
            const auto& eye = camera.GetPosition();
            const std::array<float, 3> suggested{eye[0] + viewInverse._31 * 8.0f, 0.08f,
                                                 eye[2] + viewInverse._33 * 8.0f};
            projectPanel.Draw(editState, suggested, EditWidgetsEnabled() && !pendingObject && !pendingHistory);
            if (auto* runtime = gameSession.Runtime())
                runtimeObjectPanel.Draw(runtime->World(), runtimeEditState, false, &projectPanel.Catalog(), true);
            else
            {
                objectPanel.Draw(world, editState, EditWidgetsEnabled(), &projectPanel.Catalog());
                lightmapPanel.Draw(world, editState, root, EditWidgetsEnabled());
                projectPanel.DrawInspector(editState, EditWidgetsEnabled(), &world.Layout());
            }
            if (const auto moved = projectPanel.TakeAssetMove())
            {
                try
                {
                    document.AssetMoved(root / moved->first, root / moved->second);
                }
                catch (const std::exception& exception)
                {
                    ReportStatus(exception.what(), false);
                }
            }
            if (auto request = editState.TakeRequest())
                pendingObject = std::move(request);
            if (auto scene = projectPanel.TakeSceneRequest())
                document.Request(root / *scene, false, editState.HasChanges());
            if (sceneLoaded)
            {
                const auto& json = SceneJson();
                history.Observe(json, editState.SelectedId(), editState.SelectedIds(), editState.Interaction());
                editState.SetChanged(document.UnsavedNew() || history.Dirty(json) || projectPanel.HasMaterialChanges());
            }
        }

        void DrawGamePanel()
        {
            if (focusGame)
            {
                ImGui::SetNextWindowFocus();
                focusGame = false;
            }
            gamePanel.Begin(gameTextureId, "ゲーム###Game");
            requestedGameSize = gamePanel.RequestedSize();
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                captureGamePointer = false;
            const bool pointerAllowed = Editor::GamePointerCapture::Allowed(
                captureGamePointer, gameSession.State().CanPause(), keyboard && keyboard->IsActive(),
                gamePanel.Focused(), closeRequested || ImGui::GetTopMostPopupModal() != nullptr, gamePanel.Viewport());
            if (pointerCapture.Update(keyboard ? keyboard->WindowHandle() : nullptr, gamePanel.Viewport(),
                                      pointerAllowed))
                ImGui::SetMouseCursor(ImGuiMouseCursor_None);
            Editor::ScenePanel::End();
        }

        void UpdateGameShortcut(SceneRuntime::SceneEnvironment& runtime)
        {
            const auto event = gameSession.Playback().Shortcut(runtime, playerInputs, gameTexture.GetWidth(),
                                                               gameTexture.GetHeight(), GameUiAcceptsInput());
            if (!event.object.empty())
                pendingUiEvent = event;
        }

        bool GameUiAcceptsInput() const
        {
            return gamePanel.Focused() && !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive();
        }

        void UpdateSceneTools()
        {
            const bool sceneInput = SceneEditingEnabled() && !ImGui::GetDragDropPayload();
            const auto& displayed = DisplayedWorld();
            uiCanvasPanel.SetPreviewState(gameSession.Runtime() ? gameSession.Runtime()->Ui()
                                                                : SceneRuntime::SceneUi::Defaults(displayed.Layout()));
            uiCanvasPanel.DrawScene(displayed, camera.GetCamera(), gameSession.Runtime() ? runtimeEditState : editState,
                                    showSceneUi ? sceneViewport : Editor::SceneViewport{},
                                    sceneInput && !gizmo.IsDragging() && !ikHandles.IsDragging());
            const auto selected = std::find_if(world.Layout().objects.begin(), world.Layout().objects.end(),
                                               [&](const auto& p)
                                               {
                                                   return p.id == editState.SelectedId();
                                               });
            const bool selectedUi =
                selected != world.Layout().objects.end() && selected->rectTransform && !selected->canvas;
            const bool worldInput = sceneInput && !uiCanvasPanel.ConsumesMouse() && !selectedUi;
            UpdateWorldHandles(worldInput);
            Editor::SceneSelection::Update(world, camera.GetCamera(), editState, sceneViewport,
                                           WorldSelectionAllowed(sceneInput));
        }
        void UpdateWorldHandles(bool worldInput)
        {
            ikHandles.Controls(world.Layout(), editState, sceneViewport, worldInput && !gizmo.IsDragging());
            const bool useIk = ikHandles.Enabled() && ikHandles.Available(world.Layout(), editState);
            if (useIk)
            {
                gizmo.Cancel();
                ikHandles.Draw(world, camera.GetCamera(), editState, sceneViewport,
                               worldInput && (scenePanel.Hovered() || ikHandles.IsDragging()));
            }
            else
            {
                ikHandles.Cancel();
                gizmo.UpdateAndDraw(world, camera.GetCamera(), editState, sceneViewport,
                                    worldInput && editState.InspectedAsset().empty() &&
                                        (scenePanel.Hovered() || gizmo.IsDragging()));
            }
        }

        bool WorldSelectionAllowed(bool sceneInput) const
        {
            return sceneInput && !uiCanvasPanel.ConsumesMouse() && !gizmo.ConsumesMouse() &&
                   !ikHandles.ConsumesMouse() && scenePanel.Hovered();
        }

        void DrawSaveSelectionPrefab(bool selected)
        {
            if (ImGui::MenuItem("選択をPrefabとして保存###Save selection as prefab", nullptr, false,
                                selected && editState.SingleSelection()))
            {
                const auto id = editState.SelectedId();
                std::string filename = id;
                for (auto& character : filename)
                    if (!std::isalnum(static_cast<unsigned char>(character)) && character != '-' && character != '_')
                        character = '_';
                pendingObject = Editor::ObjectRequest{Editor::ObjectAction::SavePrefab, id,
                                                      std::filesystem::path("Assets/Prefabs") / (filename + ".prefab")};
            }
        }

        void DrawPrefabMenu(bool selected, bool enabled)
        {
            if (selected && editState.SingleSelection())
            {
                const auto& objects = world.Layout().objects;
                const auto found = std::find_if(objects.begin(), objects.end(),
                                                [&](const auto& object)
                                                {
                                                    return object.id == editState.SelectedId();
                                                });
                if (found != objects.end() && found->prefab)
                {
                    if (ImGui::MenuItem("選択の変更を元Prefabへ適用###Apply prefab"))
                        pendingObject = Editor::ObjectRequest{Editor::ObjectAction::ApplyPrefab, found->prefab->rootId,
                                                              found->prefab->asset};
                    if (ImGui::MenuItem("Prefabの上書きをすべて戻す###Revert prefab"))
                        pendingObject =
                            Editor::ObjectRequest{Editor::ObjectAction::RevertPrefab, found->prefab->rootId};
                    if (ImGui::MenuItem("Prefab Variantとして保存###Save prefab variant"))
                        pendingObject =
                            Editor::ObjectRequest{Editor::ObjectAction::SavePrefabVariant, found->prefab->rootId,
                                                  found->prefab->asset.parent_path() /
                                                      (found->prefab->asset.stem().string() + "Variant.prefab")};
                    if (ImGui::MenuItem("Prefabのリンクを解除###Unpack prefab"))
                        pendingObject =
                            Editor::ObjectRequest{Editor::ObjectAction::UnpackPrefab, found->prefab->rootId};
                }
            }
            if (ImGui::MenuItem("Prefabの変更を反映###Refresh prefabs", nullptr, false, enabled))
                pendingObject = Editor::ObjectRequest{Editor::ObjectAction::RefreshPrefabs};
        }

        bool PlayToolbarEnabled() const
        {
            return sceneLoaded && IdleContextEnabled() && !reloadConfirmRequested && !newScenePopupRequested &&
                   !saveAsPanel.Requested() &&
                   !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        }
        bool HistoryEnabled() const
        {
            return !pendingPlay && gameSession.State().IsEditing() && sceneLoaded && !document.Pending() &&
                   !pendingObject && !pendingHistory && !reloadRequested && !gizmo.IsDragging() &&
                   !ikHandles.IsDragging() && !uiCanvasPanel.IsDragging() &&
                   !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        }

        void UpdateShortcuts(bool historyEnabled)
        {
            if (!CanUseShortcuts(historyEnabled))
                return;
            if (ImGui::GetIO().KeyCtrl)
                UpdateControlShortcuts();
            else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && editState.InspectedAsset().empty() &&
                     !editState.SelectedIds().empty())
                pendingObject = editState.DeleteSelectionRequest();
        }

        bool CanUseShortcuts(bool historyEnabled) const
        {
            return historyEnabled && keyboard && keyboard->IsActive() && !ImGui::IsAnyItemActive() &&
                   !ImGui::GetIO().WantTextInput && !ImGui::GetIO().KeyAlt &&
                   !ImGui::IsMouseDown(ImGuiMouseButton_Right);
        }

        bool CanUndoActive(bool redo) const
        {
            if (!editState.InspectedAsset().empty())
                return projectPanel.CanUndoAsset(editState.InspectedAsset(), redo);
            return redo ? history.CanRedo() : history.CanUndo();
        }
        void RequestUndoActive(bool redo)
        {
            if (editState.InspectedAsset().empty())
            {
                pendingHistory = redo;
                return;
            }
            if (projectPanel.UndoAsset(editState.InspectedAsset(), redo))
            {
                editState.SetChanged(document.UnsavedNew() || history.Dirty(SceneJson()) ||
                                     projectPanel.HasMaterialChanges());
                ReportStatus(redo ? "Materialの編集をやり直しました。" : "Materialの編集を元に戻しました。", true);
            }
        }
        void UpdateControlShortcuts()
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Z, false))
            {
                const bool redo = ImGui::GetIO().KeyShift;
                if (CanUndoActive(redo))
                    RequestUndoActive(redo);
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_Y, false) && CanUndoActive(true))
                RequestUndoActive(true);
            else if (ImGui::IsKeyPressed(ImGuiKey_S, false))
            {
                if (ImGui::GetIO().KeyShift)
                    saveAsPanel.Request(document.Path());
                else
                    Save();
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_D, false) && editState.InspectedAsset().empty() &&
                     !editState.SelectedIds().empty())
                pendingObject = editState.DuplicateSelectionRequest();
        }

        void DrawCommands()
        {
            Editor::PanelLayout::Place(Editor::PanelLayout::Panel::Commands);
            if (ImGui::Begin("エディター###Street Editor"))
            {
                ImGui::TextWrapped("シーン：%s", Editor::ProjectCatalog::Text(document.Path().filename()).c_str());
                ImGui::Text("オブジェクト数：%zu", world.Layout().objects.size());
                ImGui::Text("ゲーム：%s / %.2f秒", gameSession.State().Label(), gameSession.State().Elapsed());
                if (!SceneRuntime::SceneView::CameraObject(world.Layout()) &&
                    std::any_of(world.Layout().objects.begin(), world.Layout().objects.end(),
                                [](const auto& p)
                                {
                                    return p.meshRenderer || p.particleEmitter;
                                }))
                    ImGui::TextWrapped(
                        "有効なゲームカメラがありません。空のオブジェクトを作成し、カメラを追加してください。");
                ImGui::TextUnformatted(editState.HasChanges() ? "未保存の変更あり" : "保存済み・変更なし");

                if (ImGui::CollapsingHeader("ヘルプ・コンテンツ###Help / Content"))
                {
                    ImGui::TextWrapped(
                        "Ctrl+S：保存／Ctrl+D：複製／Delete：削除／1・2・3：移動・回転・拡縮／F：フォーカス");
                    ImGui::TextWrapped(
                        "再生で現在のシーンを実行します。一時停止で動きを止め、コマ送りで1フレーム進め、停止で編集状態に戻ります。");
                    ImGui::TextWrapped("コンテンツ：%s", Editor::ProjectCatalog::Text(root).c_str());
                }
                if (!audioPreview.error.empty())
                    ImGui::TextWrapped("音声：%s", audioPreview.error.c_str());
                if (!fileStatus.empty())
                    ImGui::TextWrapped("%s", fileStatus.c_str());
                if (!Editor::PanelLayout::error.empty())
                    ImGui::TextWrapped("パネル配置：%s", Editor::PanelLayout::error.c_str());
                if (reloadConfirmRequested)
                {
                    reloadConfirmRequested = false;
                    ImGui::OpenPopup("未保存の変更があります：再読み込み###Reload unsaved changes?");
                }
                DrawReloadPopup();
                DrawSceneDialogs();
            }
            ImGui::End();
            projectSettingsPanel.Draw(root, projectPanel.Catalog());
        }

        static bool EditingField()
        {
            const auto* window = ImGui::GetCurrentContext()->ActiveIdWindow;
            if (!ImGui::IsAnyItemActive() || !window)
                return false;
            if (window->Flags & ImGuiWindowFlags_ChildMenu)
                return false;
            if (std::string_view(window->Name) == "Editor toolbar")
                return false;
            return !window->ParentWindow || std::string_view(window->ParentWindow->Name) != "Editor toolbar";
        }

        bool CommandContextEnabled(bool allowToolbarText = false) const
        {
            return !pendingPlay && gameSession.State().IsEditing() && IdleContextEnabled(allowToolbarText);
        }

        bool IdleContextEnabled(bool allowToolbarText = false) const
        {
            return initialized && !pendingPlay && !document.Pending() && !pendingObject && !pendingHistory &&
                   !reloadRequested && !gizmo.IsDragging() && !ikHandles.IsDragging() && !uiCanvasPanel.IsDragging() &&
                   !ImGui::IsMouseDown(ImGuiMouseButton_Right) && !EditingField() &&
                   (!ImGui::GetIO().WantTextInput || allowToolbarText) && ImGui::GetTopMostPopupModal() == nullptr &&
                   !closeRequested;
        }

        bool CommandsEnabled(bool allowToolbarText = false) const
        {
            return sceneLoaded && CommandContextEnabled(allowToolbarText);
        }

        void RequestReload()
        {
            if (editState.HasChanges())
                reloadConfirmRequested = true;
            else
                reloadRequested = true;
        }

        void DrawMenuBar()
        {
            const bool enabled = CommandsEnabled();
            if (!ImGui::BeginMainMenuBar())
                return;
            DrawFileMenu(enabled);
            DrawEditMenu(enabled);
            if (ImGui::MenuItem("ビルド###Build player"))
                buildPanel.open = true;
            if (ImGui::MenuItem("C++ゲーム処理###Script authoring"))
                scriptPanel.open = true;
            ImGui::MenuItem("追加シーン###Scene collection", nullptr, &showSceneCollection);
            if (ImGui::BeginMenu("表示###View"))
            {
                if (ImGui::MenuItem("選択対象にフォーカス###Focus selected", "F", false,
                                    enabled && editState.InspectedAsset().empty() && sceneViewport.Valid() &&
                                        !editState.SelectedIds().empty()))
                    focusRequested = true;
                ImGui::MenuItem("性能計測###Profiler", nullptr, &profilerPanel.open);
                ImGui::MenuItem("シーンのUI###Scene UI", nullptr, &showSceneUi);
                if (ImGui::MenuItem("コンソール###Console"))
                    ImGui::SetWindowFocus("コンソール###Console");
                if (ImGui::MenuItem("ゲームタブ###Game tab", nullptr, false, enabled))
                    focusGame = true;
                if (ImGui::MenuItem("ゲーム画面を確認###Preview Game composition", nullptr, false, enabled))
                {
                    preview = true;
                    cameraPanel.CancelDrag();
                }
                if (ImGui::MenuItem("パネル配置をリセット###Reset panel layout", nullptr, false,
                                    !gizmo.IsDragging() && !ikHandles.IsDragging()))
                    Editor::PanelLayout::Reset();
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        void DrawFileMenu(bool enabled)
        {
            if (!ImGui::BeginMenu("ファイル###File"))
                return;
            if (ImGui::MenuItem("ゲームプロジェクトを開く（別ウィンドウ）…###Open game project"))
            {
                try
                {
                    Editor::GameProject::BrowseAndOpen();
                }
                catch (const std::exception& error)
                {
                    ReportStatus(error.what(), false);
                }
            }
            if (ImGui::MenuItem("新規シーン###New scene", nullptr, false, CommandContextEnabled()))
                newScenePopupRequested = true;
            DrawOpenMenu(CommandContextEnabled());
            if (ImGui::MenuItem("プロジェクト設定…###Project settings", nullptr, false, CommandContextEnabled()))
                projectSettingsPanel.Open(root);
            if (ImGui::MenuItem("保存###Save", "Ctrl+S", false, enabled))
                Save();
            if (ImGui::MenuItem("名前を付けて保存…###Save as...", "Ctrl+Shift+S", false, enabled))
                saveAsPanel.Request(document.Path());
            if (ImGui::MenuItem("再読み込み###Reload", nullptr, false, enabled && !document.UnsavedNew()))
                RequestReload();
            ImGui::Separator();
            if (ImGui::MenuItem("終了###Exit", nullptr, false, !gizmo.IsDragging() && !ikHandles.IsDragging()))
                closeRequested = true;
            ImGui::EndMenu();
        }

        void DrawOpenMenu(bool enabled)
        {
            if (!ImGui::BeginMenu("シーンを開く###Open scene", enabled))
                return;
            if (ImGui::MenuItem("シーン一覧を更新###Refresh scene list"))
                projectPanel.Scan(root);
            ImGui::Separator();
            for (const auto& asset : projectPanel.Catalog().Assets())
            {
                if (asset.kind != Editor::AssetKind::Scene)
                    continue;
                const auto path = Editor::ProjectCatalog::Text(asset.path);
                if (ImGui::MenuItem(path.c_str()))
                    document.Request(root / asset.path, false, editState.HasChanges());
            }
            ImGui::EndMenu();
        }

        void DrawEditMenu(bool enabled)
        {
            if (!ImGui::BeginMenu("編集###Edit"))
                return;
            const bool asset = !editState.InspectedAsset().empty();
            if (ImGui::MenuItem(asset ? "アセットの編集を元に戻す###Undo" : "シーンを元に戻す###Undo", "Ctrl+Z", false,
                                enabled && CanUndoActive(false)))
                RequestUndoActive(false);
            if (ImGui::MenuItem(asset ? "アセットの編集をやり直す###Redo" : "シーンをやり直す###Redo", "Ctrl+Y", false,
                                enabled && CanUndoActive(true)))
                RequestUndoActive(true);
            ImGui::Separator();
            const bool selected = enabled && editState.InspectedAsset().empty() && !editState.SelectedIds().empty();
            if (ImGui::MenuItem("複製###Duplicate", "Ctrl+D", false, selected))
                pendingObject = editState.DuplicateSelectionRequest();
            if (ImGui::MenuItem("削除###Delete", "Delete", false, selected))
                pendingObject = editState.DeleteSelectionRequest();
            ImGui::Separator();
            DrawSaveSelectionPrefab(selected);
            DrawPrefabMenu(selected, enabled);
            ImGui::EndMenu();
        }

        void DrawPlayToolbar()
        {
            const bool enabled = PlayToolbarEnabled();
            ImGui::BeginDisabled(!enabled);
            ImGui::BeginDisabled(!gameSession.State().CanPlay() || !scriptPanel.Ready());
            if (ImGui::Button(gameSession.State().IsEditing() ? "再生###Play" : "再開###Play"))
                pendingPlay = Editor::GameSession::Command::Play;
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!gameSession.State().CanPause());
            if (ImGui::Button("一時停止###Pause"))
                pendingPlay = Editor::GameSession::Command::Pause;
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!gameSession.State().CanStop());
            if (ImGui::Button("停止###Stop"))
                pendingPlay = Editor::GameSession::Command::Stop;
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!gameSession.State().CanStep());
            if (ImGui::Button("コマ送り###Step"))
                pendingPlay = Editor::GameSession::Command::Step;
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextUnformatted(gameSession.State().Label());
            ImGui::SameLine();
            ImGui::BeginDisabled(!gameSession.State().CanStop());
            ImGui::Checkbox("マウス捕捉###Capture Game pointer", &captureGamePointer);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Gameにフォーカスするとマウスを表示領域内に捕捉します。Escapeで解除します。");
            ImGui::EndDisabled();
            if (auto* runtime = gameSession.Runtime(); runtime && gameSession.State().CanStep())
            {
                float time = runtime->Ui().Value("sceneTime");
                float start = runtime->Ui().Value("startTime", -1);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(120);
                const bool seek = ImGui::DragFloat("演出時刻###Animation time", &time, 0.01f, 0, 600, "%.2f秒",
                                                   ImGuiSliderFlags_AlwaysClamp);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(100);
                const bool starting = ImGui::DragFloat("開始時刻###Start time", &start, 0.01f, -1, 600, "%.2f秒",
                                                       ImGuiSliderFlags_AlwaysClamp);
                if (seek || starting)
                    runtime->SeekAnimation(time, time, start);
            }
        }

        void DrawToolbar()
        {
            const auto flags =
                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollbar;
            const float height = ImGui::GetFrameHeight() + 2 * ImGui::GetStyle().WindowPadding.y;
            if (ImGui::BeginViewportSideBar("Editor toolbar", ImGui::GetMainViewport(), ImGuiDir_Up, height, flags))
            {
                DrawPlayToolbar();
                ImGui::SameLine();
                const bool enabled = CommandsEnabled() && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId |
                                                                                      ImGuiPopupFlags_AnyPopupLevel);
                ImGui::BeginDisabled(!enabled);
                if (ImGui::Button("保存###Save"))
                    Save();
                ImGui::SameLine();
                ImGui::BeginDisabled(!CanUndoActive(false));
                if (ImGui::Button("元に戻す###Undo"))
                    RequestUndoActive(false);
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(!CanUndoActive(true));
                if (ImGui::Button("やり直す###Redo"))
                    RequestUndoActive(true);
                ImGui::EndDisabled();
                ImGui::EndDisabled();
                ImGui::SameLine();
                gizmo.DrawToolbar(CommandsEnabled(true) && editState.InspectedAsset().empty());
            }
            ImGui::End();
        }

        void DrawSceneDialogs()
        {
            DrawSceneCollection();
            saveAsPanel.Draw(
                root,
                [&](const auto& path, bool overwrite)
                {
                    return SaveAs(path, overwrite);
                },
                fileStatus);
            if (newScenePopupRequested)
            {
                ImGui::OpenPopup("新規シーン###New scene");
                newScenePopupRequested = false;
            }
            if (ImGui::BeginPopupModal("新規シーン###New scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextUnformatted("Assets/Scenes内のファイル名（保存時に作成）：");
                ImGui::InputText("ファイル名###Filename", newSceneName.data(), newSceneName.size());
                if (ImGui::Button("作成###Create"))
                    CreateSceneRequest();
                ImGui::SameLine();
                if (ImGui::Button("キャンセル###Cancel"))
                    ImGui::CloseCurrentPopup();
                if (!audioPreview.error.empty())
                    ImGui::TextWrapped("音声：%s", audioPreview.error.c_str());
                if (!fileStatus.empty())
                    ImGui::TextWrapped("%s", fileStatus.c_str());
                ImGui::EndPopup();
            }
            if (document.NeedsConfirmation() &&
                !ImGui::IsPopupOpen("未保存の変更があります：シーン切り替え###Switch scene with unsaved changes?"))
                ImGui::OpenPopup("未保存の変更があります：シーン切り替え###Switch scene with unsaved changes?");
            DrawSceneSwitchPopup();
        }

        void DrawSceneCollection()
        {
            if (!showSceneCollection)
                return;
            if (ImGui::Begin("追加シーン###Scene collection", &showSceneCollection))
            {
                ImGui::InputText("Assets/Scenes内のパス###Scene path", additiveScenePath.data(),
                                 additiveScenePath.size());
                ImGui::BeginDisabled(pendingSceneCollection.has_value() || !sceneLoaded);
                if (ImGui::Button("追加ロード###Load additive"))
                {
                    const std::filesystem::path path(additiveScenePath.data());
                    if (!path.is_absolute() && path.generic_string().starts_with("Assets/Scenes/") &&
                        path.extension() == ".json" &&
                        std::none_of(path.begin(), path.end(),
                                     [](const auto& part)
                                     {
                                         return part == "..";
                                     }))
                        pendingSceneCollection = {{path.generic_string(), false}};
                    else
                        fileStatus = "Assets/Scenes内のJSONを指定してください。";
                }
                SceneRuntime::SceneCollection collection;
                if (gameSession.Runtime())
                    collection = gameSession.Runtime()->Scenes();
                else
                    collection.Reset(world.Layout(), document.Path().lexically_relative(root).generic_string());
                for (const auto& entry : collection.Entries())
                {
                    ImGui::PushID(entry.name.c_str());
                    ImGui::TextUnformatted(entry.name.c_str());
                    ImGui::SameLine();
                    if (ImGui::SmallButton("アンロード###Unload"))
                        pendingSceneCollection = {{entry.name, true}};
                    ImGui::PopID();
                }
                ImGui::EndDisabled();
                ImGui::TextWrapped(
                    "Inspectorで保持を指定すると、シーン切り替え後も対象と子孫を保持します。編集時の保存は合成シーンとして保存します。");
                if (!fileStatus.empty())
                    ImGui::TextWrapped("%s", fileStatus.c_str());
            }
            ImGui::End();
        }

        void CreateSceneRequest()
        {
            try
            {
                const auto path = Editor::SceneDocument::NewTarget(root, newSceneName.data());
                document.Request(path, true, editState.HasChanges());
                ImGui::CloseCurrentPopup();
            }
            catch (const std::exception& error)
            {
                fileStatus = error.what();
                LogResult(false);
            }
        }

        void DrawSceneSwitchPopup()
        {
            if (!ImGui::BeginPopupModal("未保存の変更があります：シーン切り替え###Switch scene with unsaved changes?",
                                        nullptr, ImGuiWindowFlags_AlwaysAutoResize))
                return;
            ImGui::TextUnformatted("現在のシーンに未保存の変更があります。");
            if (ImGui::Button("保存して続行###Save and continue"))
            {
                if (Save())
                {
                    document.Confirm();
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("変更を破棄して続行###Discard and continue"))
            {
                document.Confirm();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("キャンセル###Cancel"))
            {
                document.Cancel();
                ImGui::CloseCurrentPopup();
            }
            if (!fileStatus.empty())
                ImGui::TextWrapped("%s", fileStatus.c_str());
            ImGui::EndPopup();
        }

        void DrawReloadPopup()
        {
            if (ImGui::BeginPopupModal("未保存の変更があります：再読み込み###Reload unsaved changes?", nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextUnformatted("現在のシーンに未保存の変更があります。");
                if (ImGui::Button("保存して再読み込み###Save and reload"))
                {
                    if (Save())
                    {
                        reloadRequested = true;
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("変更を破棄して再読み込み###Discard and reload"))
                {
                    reloadRequested = true;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("キャンセル###Cancel"))
                    ImGui::CloseCurrentPopup();
                if (!audioPreview.error.empty())
                    ImGui::TextWrapped("音声：%s", audioPreview.error.c_str());
                if (!fileStatus.empty())
                    ImGui::TextWrapped("%s", fileStatus.c_str());
                ImGui::EndPopup();
            }
        }

        const std::filesystem::path root;
        const std::shared_ptr<const Engine::ContentContext> context = Engine::ContentContext::Capture(root);
        Editor::SceneDocument document{std::filesystem::path{}, context};
        SceneRuntime::SceneWorld world{document.Data()};
        bool editingUiReady = false;
        Engine::DebugCamera camera;
        Editor::CameraPanel cameraPanel;
        Engine::RenderTexture gameTexture;
        Editor::ScenePanel gamePanel;
        UINT64 gameTextureId = 0;
        std::array<UINT, 2> requestedGameSize{640, 480};
        bool focusGame = false;
        Engine::RenderTexture sceneTexture;
        UINT64 sceneTextureId = 0;
        std::array<UINT, 2> requestedSceneSize{640, 480};
        Editor::ScenePanel scenePanel;
        Editor::SceneViewport sceneViewport;
        Editor::EditState editState;
        std::optional<SceneRuntime::UiEvent> pendingUiEvent;
        Editor::AudioPreview audioPreview;
        Editor::UiCanvasPanel uiCanvasPanel;
        bool showSceneUi = true;
        std::string viewSignature;
        Editor::UiEditorPanel uiEditorPanel;
        Engine::RenderTexture uiTexture;
        UINT64 uiTextureId = 0;
        Editor::GameSession gameSession;
        Editor::GamePointerCapture pointerCapture;
        bool captureGamePointer = false;
        std::optional<Editor::GameSession::Command> pendingPlay;
        Editor::ObjectPanel objectPanel;
        Editor::ObjectPanel runtimeObjectPanel;
        Editor::EditState runtimeEditState;
        Editor::SceneRecovery recovery;
        bool pendingRecovery = false;
        Editor::ProjectPanel projectPanel;
        Editor::BuildPanel buildPanel;
        Editor::ScriptAuthoringPanel scriptPanel;
        Editor::ProfilerPanel profilerPanel;
        Editor::AssetChanges assetChanges;
        bool assetReloadRequested = false;
        Editor::TransformGizmo gizmo;
        Editor::IkSceneHandles ikHandles;
        Editor::EditHistory history;
        Editor::ConsolePanel consolePanel;
        Editor::SaveAsPanel saveAsPanel;
        Editor::ProjectSettingsPanel projectSettingsPanel;
        Engine::InputActions playerInputs;
        Engine::InputSnapshot playerSnapshot;
        Engine::Gamepad gamepad;
        std::unique_ptr<SceneRuntime::ScenePresentation> presentation;
        bool preview = false;
        bool focusRequested = false;
        std::optional<bool> pendingHistory;
        std::optional<Editor::ObjectRequest> pendingObject;
        const Engine::Keyboard* keyboard = nullptr;
        double seconds = 0.0;
        bool initialized = false;
        bool sceneLoaded = false;
        bool reloadRequested = false;
        bool reloadConfirmRequested = false;
        bool closeRequested = false;
        bool closeConfirmed = false;
        std::string fileStatus;
        Editor::SceneSnapshotCache sceneSnapshots;
        bool newScenePopupRequested = false;
        std::array<char, 256> newSceneName{"NewScene.json"};
        bool showSceneCollection = false;
        Editor::LightmapPanel lightmapPanel;
        std::array<char, 256> additiveScenePath{"Assets/Scenes/Game.json"};
        std::optional<std::pair<std::string, bool>> pendingSceneCollection;
    };
}

static void ReadEditorArguments(int count, wchar_t** arguments, std::filesystem::path& project,
                                std::filesystem::path& scene, bool& validateProject)
{
    for (int i = 1; i < count; ++i)
    {
        const std::wstring_view argument(arguments[i]);
        if (argument == L"--project" && i + 1 < count)
            project = arguments[++i];
        else if (argument.starts_with(L"--project="))
            project = argument.substr(10);
        else if (argument == L"--scene" && i + 1 < count)
            scene = arguments[++i];
        else if (argument.starts_with(L"--scene="))
            scene = argument.substr(8);
        else if (argument == L"--validate-project")
            validateProject = true;
        else
            throw std::runtime_error("Usage: Editor.exe [--project folder] [--scene Assets/Scenes/name.json]");
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    bool validateProject = false;
    try
    {
        std::filesystem::path scene, project;
        int count = 0;
        auto arguments = CommandLineToArgvW(GetCommandLineW(), &count);
        if (!arguments)
            throw std::runtime_error("Cannot parse editor arguments.");
        try
        {
            ReadEditorArguments(count, arguments, project, scene, validateProject);
        }
        catch (...)
        {
            LocalFree(arguments);
            throw;
        }
        LocalFree(arguments);
        if (validateProject && project.empty())
            throw std::runtime_error("Project validation requires --project.");
        const auto content = project.empty() ? DefaultGameRoot() : Editor::GameProject::Open(project);
        if (content.empty())
            return 0;
        if (validateProject)
        {
            if (project.empty())
                throw std::runtime_error("Project validation requires --project.");
            const auto settings = SceneRuntime::ProjectSettings::Load(content);
            SceneRuntime::SceneLayout::Load(content / Engine::AssetDatabase::Path(settings.startupScene), content);
            return 0;
        }
        EditorApplication editor(content, scene);
        return editor.Run();
    }
    catch (const std::exception& error)
    {
        if (validateProject)
        {
            Engine::Log::Error(error.what());
            return 1;
        }
        MessageBoxA(nullptr, error.what(), "Editor startup error", MB_OK | MB_ICONERROR);
        return 1;
    }
}
