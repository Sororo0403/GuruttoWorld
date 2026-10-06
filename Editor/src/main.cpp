#include "CameraPanel.h"
#include "ObjectPanel.h"
#include "ProjectPanel.h"
#include "ModelDrop.h"
#include "SceneSelection.h"
#include "TransformGizmo.h"
#include "EditHistory.h"
#include "FocusSelection.h"
#include "PanelLayout.h"
#include "ScenePanel.h"
#include "ConsolePanel.h"
#include "SceneDocument.h"
#include "SaveAsPanel.h"
#include "GameSession.h"
#include <Engine/Graphics/Resources/RenderTexture.h>
#include <SceneRuntime/TitleView.h>
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
        if (!length || length >= executable.size()) return {};
        executable.resize(length);
        auto directory = std::filesystem::path(executable).parent_path();
        // 開発時は元のContentを使い、将来の保存先もビルド出力のコピーにしません。
        for (auto parent = directory; !parent.empty(); parent = parent.parent_path())
        {
            if (std::filesystem::exists(parent / "Content/Assets/Models/Title")) return parent / "Content";
            if (parent == parent.parent_path()) break;
        }
        return directory; // 配布時はEditorに同梱されたContentを読みます。
    }

    class StreetEditor final
    {
    public:
        int Run()
        {
            if (root.empty()) return 1;
            camera.SetResetPose({ -0.8f, 2.8f, -7.0f }, 0.03f, 0.09f);
            camera.SetMoveSpeed(8.0f);
            SceneRuntime::TitleView::SetHome(previewCamera);
            projectPanel.Scan(root);
            Engine::ApplicationCallbacks callbacks;
            callbacks.closeRequested = [&]() { closeRequested = true; };
            callbacks.shouldClose = [&]()
            {
                if (closeConfirmed) Editor::PanelLayout::Save();
                return closeConfirmed;
            };
            callbacks.update = [&](double dt, const Engine::Keyboard& input)
            {
                keyboard = &input;
                seconds = dt;
                if (!input.IsActive()) cameraPanel.CancelDrag();
            };
            callbacks.draw = [&](Engine::DirectX12Renderer& renderer) { return Draw(renderer); };
            Engine::ApplicationSettings settings;
            settings.title = L"WP1 Street Editor";
            Engine::Application application;
            return application.Run(settings, callbacks);
        }

    private:
        Engine::RenderResult Draw(Engine::DirectX12Renderer& renderer)
        {
            if (!ApplyPendingChanges(renderer) || !PrepareSceneTexture(renderer) || !PrepareGameTexture(renderer)) return Engine::RenderResult::Failed;
            gameSession.Update(seconds,keyboard && keyboard->IsActive() && !closeRequested);
            bool rendered = true;
            const auto result = renderer.Render({0.10f, 0.11f, 0.13f, 1},
                [&](ID3D12GraphicsCommandList* commands, float aspect)
                {
                    if (preview)
                    {
                        SceneRuntime::TitleView::SetProjection(previewCamera, aspect);
                        world.Draw(commands, previewCamera, light);
                    }
                    else
                    {
                        if (sceneViewport.Valid()) rendered = DrawSceneTexture(commands);
                        if (gamePanel.Viewport().Valid()) rendered = DrawGameTexture(commands) && rendered;
                    }
                }, [&]() { DrawUi(); });
            return rendered ? result : Engine::RenderResult::Failed;
        }

        void ReportStatus(std::string message, bool success)
        {
            if (message==fileStatus) return;
            fileStatus=std::move(message);
            LogResult(success);
        }

        void LogResult(bool success) const
        {
            if (!fileStatus.empty()) Engine::Log::Write(success ? Engine::LogLevel::Info : Engine::LogLevel::Error,fileStatus);
        }

        bool PrepareSceneTexture(Engine::DirectX12Renderer& renderer)
        {
            if (!requestedSceneSize[0] || !requestedSceneSize[1]) return true;
            if (sceneTexture.GetWidth() == requestedSceneSize[0] && sceneTexture.GetHeight() == requestedSceneSize[1]) return true;
            if (!sceneTexture.Resize(renderer, requestedSceneSize[0], requestedSceneSize[1]))
            {
                ReportStatus("Could not resize the Scene render texture.",false);
                return sceneTexture.GetResource() != nullptr;
            }
            sceneTextureId = renderer.SetSceneTexture(sceneTexture.GetShaderResourceView()).ptr;
            return sceneTextureId != 0;
        }

        bool PrepareGameTexture(Engine::DirectX12Renderer& renderer)
        {
            if (!requestedGameSize[0] || !requestedGameSize[1]) return true;
            if (gameTexture.GetWidth()==requestedGameSize[0] && gameTexture.GetHeight()==requestedGameSize[1]) return true;
            if (!gameTexture.Resize(renderer,requestedGameSize[0],requestedGameSize[1]))
            {
                ReportStatus("Could not resize the Game render texture.",false);
                return gameTexture.GetResource()!=nullptr;
            }
            gameTextureId=renderer.SetSceneTexture(gameTexture.GetShaderResourceView(),1).ptr;
            return gameTextureId!=0;
        }

        bool DrawGameTexture(ID3D12GraphicsCommandList* commands)
        {
            if (!gameTexture.Begin(commands,{0.66f,0.79f,0.83f,1})) return false;
            SceneRuntime::TitleView::SetProjection(previewCamera,gamePanel.Viewport().Aspect());
            if (gameSession.Runtime()) gameSession.Draw(commands,gameTexture.GetWidth(),gameTexture.GetHeight());
            else world.Draw(commands,previewCamera,light);
            return gameTexture.End(commands);
        }

        bool DrawSceneTexture(ID3D12GraphicsCommandList* commands)
        {
            if (!sceneTexture.Begin(commands, {0.66f, 0.79f, 0.83f, 1})) return false;
            world.Draw(commands, camera.GetCamera(), light);
            return sceneTexture.End(commands);
        }

        bool ApplyReload(Engine::DirectX12Renderer& renderer)
        {
            if (reloadRequested)
            {
                reloadRequested = false;
                if (!renderer.WaitForIdle()) return false;
                if (world.Reload(root, document.Path(), fileStatus))
                {
                    sceneLoaded = true;
                    editState.Reloaded();
                    history.Reset(Snapshot(world.Layout().Serialize()));
                    fileStatus = "Reloaded.";
                    LogResult(true);
                }
                else LogResult(false);
            }
            return true;
        }

        bool ApplySceneChange(Engine::DirectX12Renderer& renderer)
        {
            if (!document.Ready()) return true;
            if (!renderer.WaitForIdle()) return false;
            if (document.Apply(world,root,fileStatus))
            {
                sceneLoaded=true;
                editState.Reloaded();
                history.Reset({world.Layout().Serialize(),{}});
                editState.SetChanged(document.UnsavedNew());
                cameraPanel.CancelDrag();
                fileStatus=document.UnsavedNew() ? "New scene (not saved yet)." : "Scene opened.";
                LogResult(true);
            }
            else LogResult(false);
            return true;
        }

        Editor::EditHistory::State Snapshot(const std::string& json) const
        {
            return {json,editState.SelectedId(),editState.SelectedIds()};
        }

        bool ApplyHistory(Engine::DirectX12Renderer& renderer)
        {
            if (pendingHistory)
            {
                if (!renderer.WaitForIdle()) return false;
                const bool redo=*pendingHistory;
                pendingHistory.reset();
                const auto target=history.Target(redo);
                if (world.ReplaceLayout(SceneRuntime::SceneLayout::Parse(target.json), root, fileStatus))
                {
                    history.Applied(redo);
                    editState.RestoreSelection(target.selections,target.selection);
                    editState.SetChanged(document.UnsavedNew() || history.Dirty(target.json));
                    fileStatus=redo ? "Redone." : "Undone.";
                    LogResult(true);
                }
                else LogResult(false);
            }
            return true;
        }

        bool ApplyObject(Engine::DirectX12Renderer& renderer)
        {
            if (pendingObject)
            {
                if (!renderer.WaitForIdle()) return false;
                history.Commit();
                auto request = std::move(*pendingObject);
                pendingObject.reset();
                std::string createdId;
                bool success = false;
                if (request.action == Editor::ObjectAction::Add)
                {
                    SceneRuntime::ScenePlacement placement;
                    placement.model = request.model;
                    placement.position = request.position;
                    placement.scale = { 4, 4, 4 };
                    success = world.AddObject(std::move(placement), root, createdId, fileStatus);
                }
                else if (request.action == Editor::ObjectAction::Duplicate)
                    success = editState.DuplicateObjects(world,request.ids,{4,0,0},fileStatus);
                else
                {
                    success = editState.DeleteObjects(world,request.ids,fileStatus);
                }
                if (success)
                {
                    if (request.action==Editor::ObjectAction::Add) editState.ObjectChanged(createdId);
                    fileStatus = request.action == Editor::ObjectAction::Delete ? "Deleted." :
                        request.action == Editor::ObjectAction::Duplicate ? "Duplicated." : "Added.";
                    history.Observe(Snapshot(world.Layout().Serialize()), {});
                }
                LogResult(success);
            }
            return true;
        }

        bool ApplyPendingChanges(Engine::DirectX12Renderer& renderer)
        {
            if (!initialized)
            {
                const auto settingsRoot = Engine::GetDiagnosticsRoot();
                Editor::PanelLayout::Initialize(settingsRoot.empty() ? std::filesystem::path{} : settingsRoot / "Editor/layout.ini");
                sceneLoaded = world.Initialize(renderer, root, document.Path(),
                    root / "Shaders/TitleMesh.hlsl", &fileStatus);
                initialized = true;
                if (sceneLoaded) history.Reset(Snapshot(world.Layout().Serialize()));
                Engine::Log::Write(sceneLoaded ? Engine::LogLevel::Info : Engine::LogLevel::Error,
                    sceneLoaded ? "Editor scene loaded." : fileStatus);
            }
            return ApplySceneChange(renderer) && ApplyReload(renderer) && ApplyHistory(renderer) && ApplyObject(renderer) && ApplyPlay(renderer);
        }

        bool ApplyPlay(Engine::DirectX12Renderer& renderer)
        {
            if (!pendingPlay) return true;
            const auto command=*pendingPlay;
            pendingPlay.reset();
            if (command==Editor::GameSession::Command::Pause)
            {
                if (gameSession.Pause()) ReportStatus("Game paused.",true);
                return true;
            }
            if (command==Editor::GameSession::Command::Step)
            {
                if (gameSession.Step()) ReportStatus("Stepped one frame (1/60 s).",true);
                return true;
            }
            if (!renderer.WaitForIdle()) return false;
            if (command==Editor::GameSession::Command::Stop)
            {
                if (gameSession.Stop()) ReportStatus("Returned to editing.",true);
                return true;
            }
            if (!gameSession.Play(renderer,root,world.Layout(),fileStatus)) { LogResult(false); return true; }
            history.Commit();
            focusGame=true;
            cameraPanel.CancelDrag();
            ReportStatus("Playing.",true);
            return true;
        }

        bool Save()
        {
            if (!sceneLoaded) { fileStatus = "No scene is loaded to save."; LogResult(false); return false; }
            try
            {
                document.Save(world.Layout());
                SavedSuccessfully("Saved.");
                return true;
            }
            catch (const std::exception& error)
            {
                fileStatus = std::string("Save failed: ") + error.what();
                LogResult(false);
                return false;
            }
        }

        void SavedSuccessfully(std::string status)
        {
            history.Saved(world.Layout().Serialize());
            editState.MarkSaved();
            projectPanel.Scan(root);
            fileStatus=std::move(status);
            LogResult(true);
        }

        bool SaveAs(const std::filesystem::path& target, bool overwrite)
        {
            if (!sceneLoaded) return false;
            try
            {
                document.SaveAs(world.Layout(),target,overwrite);
                SavedSuccessfully("Saved as: "+Editor::ProjectCatalog::Text(document.Path().filename()));
                return true;
            }
            catch (const std::exception& error)
            {
                fileStatus=std::string("Save as failed: ")+error.what();
                LogResult(false);
                return false;
            }
        }

        void DrawUi()
        {
            if (!preview)
            {
                DrawMenuBar();
                DrawToolbar();
            }
            Editor::PanelLayout::BeginFrame(preview);
            if (!preview) consolePanel.Draw();
            Editor::TransformGizmo::BeginFrame();
            sceneViewport = {};
            if (preview)
            {
                if (closeRequested) DrawClosePopup();
                else DrawPreview();
                return;
            }
            if (focusGame) { ImGui::SetNextWindowFocus(); focusGame=false; }
            gamePanel.Begin(gameTextureId,"Game (title composition)###Game");
            requestedGameSize=gamePanel.RequestedSize();
            Editor::ScenePanel::End();
            scenePanel.Begin(sceneTextureId);
            sceneViewport = scenePanel.Viewport();
            requestedSceneSize = scenePanel.RequestedSize();
            AcceptModelDrop();
            if (closeRequested) { DrawClosePopup(); Editor::ScenePanel::End(); return; }
            UpdateCamera();
            const bool canFocus = CanFocus();
            UpdateGizmoShortcuts(canFocus);
            FocusSelection(canFocus);
            UpdateObjects();
            const bool historyEnabled = HistoryEnabled();
            UpdateShortcuts(historyEnabled);
            DrawCommands();
            if (!preview) Editor::SceneSelection::Draw(world, camera.GetCamera(), editState, sceneViewport);
            Editor::ScenePanel::End();
        }

        void AcceptModelDrop()
        {
            if (closeRequested || !SceneEditingEnabled() || gizmo.IsDragging() ||
                ImGui::IsMouseDown(ImGuiMouseButton_Right) || !sceneViewport.Valid()) return;
            if (!ImGui::BeginDragDropTarget()) return;
            if (const auto* payload=ImGui::AcceptDragDropPayload(Editor::ModelPayload))
            {
                const auto* path=static_cast<const char*>(payload->Data);
                if (payload->DataSize>1 && path[payload->DataSize-1]=='\0' &&
                    std::char_traits<char>::length(path)==static_cast<size_t>(payload->DataSize-1))
                {
                    SceneRuntime::TitleView::SetProjection(camera.GetCamera(),sceneViewport.Aspect());
                    const auto mouse=ImGui::GetIO().MousePos;
                    if (const auto position=Editor::ModelDropPosition(camera.GetCamera(),sceneViewport,mouse.x,mouse.y))
                        projectPanel.RequestDrop(editState,path,*position);
                }
            }
            ImGui::EndDragDropTarget();
        }

        void DrawClosePopup()
        {
            cameraPanel.CancelDrag();
            if (!sceneLoaded || !editState.HasChanges())
            {
                closeConfirmed = true;
                return;
            }
            history.Observe(Snapshot(world.Layout().Serialize()), {});
            // 編集中のギズモを止め、確認中は配置を変更しません。
            gizmo.UpdateAndDraw(world, camera.GetCamera(), editState, sceneViewport, false);
            if (!ImGui::IsPopupOpen("Exit with unsaved changes?")) ImGui::OpenPopup("Exit with unsaved changes?");
            if (ImGui::BeginPopupModal("Exit with unsaved changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextUnformatted("The current scene has unsaved changes.");
                if (ImGui::Button("Save and exit"))
                {
                    if (Save()) { closeConfirmed = true; ImGui::CloseCurrentPopup(); }
                }
                ImGui::SameLine();
                if (ImGui::Button("Exit without saving"))
                {
                    closeConfirmed = true;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel"))
                {
                    closeRequested = false;
                    ImGui::CloseCurrentPopup();
                }
                if (!fileStatus.empty()) ImGui::TextWrapped("%s", fileStatus.c_str());
                ImGui::EndPopup();
            }
        }

        void DrawPreview()
        {
            if (keyboard && keyboard->IsActive() && ImGui::IsKeyPressed(ImGuiKey_Escape,false)) preview=false;
            ImGui::SetNextWindowPos(ImVec2(20,20),ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.8f);
            if (ImGui::Begin("Title composition", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextUnformatted("Title camera / lighting - current layout (including unsaved edits)");
                ImGui::TextUnformatted("Fixed view; sky, particles and title UI are excluded.");
                if (ImGui::Button("Back to editing (Escape)")) preview=false;
            }
            ImGui::End();
        }

        void UpdateCamera()
        {
            if (keyboard) cameraPanel.Draw(camera, *keyboard, seconds, sceneViewport, scenePanel.Hovered(), !gizmo.IsDragging() && !ImGui::GetDragDropPayload());
            if (sceneViewport.Valid())
                SceneRuntime::TitleView::SetProjection(camera.GetCamera(), sceneViewport.Aspect());
        }

        bool CanFocus() const
        {
            return !pendingPlay && sceneViewport.Valid() && !document.Pending() && sceneLoaded && keyboard && keyboard->IsActive() && !gizmo.IsDragging() &&
                !pendingObject && !pendingHistory && !reloadRequested &&
                !ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) &&
                !ImGui::IsMouseDown(ImGuiMouseButton_Right);
        }

        void UpdateGizmoShortcuts(bool canFocus)
        {
            if (canFocus && !ImGui::IsAnyItemActive() && !ImGui::GetIO().WantTextInput &&
                !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyAlt)
            {
                if (ImGui::IsKeyPressed(ImGuiKey_1,false)) gizmo.SetMode(Editor::TransformGizmo::Mode::Move);
                if (ImGui::IsKeyPressed(ImGuiKey_2,false)) gizmo.SetMode(Editor::TransformGizmo::Mode::Rotate);
                if (ImGui::IsKeyPressed(ImGuiKey_3,false)) gizmo.SetMode(Editor::TransformGizmo::Mode::Scale);
            }
        }

        void FocusSelection(bool canFocus)
        {
            if (focusRequested || (canFocus && !ImGui::IsAnyItemActive() && !ImGui::GetIO().WantTextInput &&
                !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyAlt && ImGui::IsKeyPressed(ImGuiKey_F,false)))
            {
                focusRequested=false;
                if (canFocus && !editState.SelectedIds().empty())
                {
                    if (const auto position=Editor::FocusPosition(world,editState.SelectedIds(),camera.GetCamera()))
                    {
                        cameraPanel.CancelDrag();
                        camera.GetCamera().SetPosition(*position);
                    }
                    else fileStatus="Selection cannot fit within the camera range, or an object is unavailable.";
                }
            }
        }

        bool EditWidgetsEnabled() const
        {
            return !pendingPlay && gameSession.State().IsEditing() && sceneLoaded && !document.Pending() && !reloadRequested && !gizmo.IsDragging() &&
                ImGui::GetTopMostPopupModal()==nullptr;
        }

        bool SceneEditingEnabled() const
        {
            return !pendingPlay && gameSession.State().IsEditing() && sceneLoaded && !document.Pending() && !reloadRequested && keyboard && keyboard->IsActive() &&
                !pendingObject && !pendingHistory && !reloadConfirmRequested && ImGui::GetTopMostPopupModal()==nullptr;
        }

        void UpdateObjects()
        {
            editState.BeginFrame();
            const bool sceneInput=SceneEditingEnabled() && !ImGui::GetDragDropPayload();
            gizmo.UpdateAndDraw(world, camera.GetCamera(), editState, sceneViewport,
                sceneInput && (scenePanel.Hovered() || gizmo.IsDragging()));
            Editor::SceneSelection::Update(world, camera.GetCamera(), editState, sceneViewport,
                sceneInput && !gizmo.ConsumesMouse() && scenePanel.Hovered());
            DirectX::XMFLOAT4X4 viewInverse;
            DirectX::XMStoreFloat4x4(&viewInverse, DirectX::XMMatrixInverse(nullptr, camera.GetViewMatrix()));
            const auto& eye = camera.GetPosition();
            const std::array<float, 3> suggested{ eye[0] + viewInverse._31 * 8.0f, 0.08f,
                eye[2] + viewInverse._33 * 8.0f };
            objectPanel.Draw(world, editState, EditWidgetsEnabled());
            projectPanel.Draw(editState, suggested, EditWidgetsEnabled() && !pendingObject && !pendingHistory);
            if (auto request = editState.TakeRequest()) pendingObject = std::move(request);
            if (auto scene=projectPanel.TakeSceneRequest()) document.Request(root / *scene,false,editState.HasChanges());
            if (sceneLoaded)
            {
                const auto json=world.Layout().Serialize();
                history.Observe(Snapshot(json), editState.Interaction());
                editState.SetChanged(document.UnsavedNew() || history.Dirty(json));
            }
        }

        bool HistoryEnabled() const
        {
            return !pendingPlay && gameSession.State().IsEditing() && sceneLoaded && !document.Pending() && !pendingObject && !pendingHistory && !reloadRequested &&
                !gizmo.IsDragging() && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        }

        void UpdateShortcuts(bool historyEnabled)
        {
            if (!CanUseShortcuts(historyEnabled)) return;
            if (ImGui::GetIO().KeyCtrl) UpdateControlShortcuts();
            else if (ImGui::IsKeyPressed(ImGuiKey_Delete,false) && !editState.SelectedIds().empty())
                pendingObject=editState.DeleteSelectionRequest();
        }

        bool CanUseShortcuts(bool historyEnabled) const
        {
            return historyEnabled && keyboard && keyboard->IsActive() &&
                !ImGui::IsAnyItemActive() && !ImGui::GetIO().WantTextInput && !ImGui::GetIO().KeyAlt &&
                !ImGui::IsMouseDown(ImGuiMouseButton_Right);
        }

        void UpdateControlShortcuts()
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Z, false))
            {
                const bool redo=ImGui::GetIO().KeyShift;
                if (redo ? history.CanRedo() : history.CanUndo()) pendingHistory=redo;
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_Y, false) && history.CanRedo()) pendingHistory=true;
            else if (ImGui::IsKeyPressed(ImGuiKey_S,false))
            {
                if (ImGui::GetIO().KeyShift) saveAsPanel.Request(document.Path()); else Save();
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_D,false) && !editState.SelectedIds().empty())
                pendingObject=editState.DuplicateSelectionRequest();
        }

        void DrawCommands()
        {
            Editor::PanelLayout::Place(Editor::PanelLayout::Panel::Commands);
            if (ImGui::Begin("Street Editor"))
            {
                ImGui::TextWrapped("Scene: %s",Editor::ProjectCatalog::Text(document.Path().filename()).c_str());
                ImGui::Text("Objects: %zu", world.Layout().objects.size());
                ImGui::Text("Game: %s / %.2f s",gameSession.State().Label(),gameSession.State().Elapsed());
                ImGui::TextUnformatted(editState.HasChanges() ? "Unsaved changes" : "Saved / unchanged");

                if (ImGui::CollapsingHeader("Help / Content"))
                {
                    ImGui::TextWrapped("Ctrl+S: Save / Ctrl+D: Duplicate / Delete: Remove / 1,2,3: Move,Rotate,Scale / F: Focus");
                    ImGui::TextWrapped("Play runs the current layout with the title background. Pause freezes Game; Resume continues. Stop returns to the edit preview.");
                    ImGui::TextWrapped("Content: %s",root.string().c_str());
                }
                if (!fileStatus.empty()) ImGui::TextWrapped("%s", fileStatus.c_str());
                if (!Editor::PanelLayout::error.empty()) ImGui::TextWrapped("Layout: %s", Editor::PanelLayout::error.c_str());
                if (reloadConfirmRequested)
                {
                    reloadConfirmRequested=false;
                    ImGui::OpenPopup("Reload unsaved changes?");
                }
                DrawReloadPopup();
                DrawSceneDialogs();
            }
            ImGui::End();
        }

        static bool EditingField()
        {
            const auto* window=ImGui::GetCurrentContext()->ActiveIdWindow;
            if (!ImGui::IsAnyItemActive() || !window) return false;
            if (window->Flags & ImGuiWindowFlags_ChildMenu) return false;
            if (std::string_view(window->Name)=="Editor toolbar") return false;
            return !window->ParentWindow || std::string_view(window->ParentWindow->Name)!="Editor toolbar";
        }

        bool CommandContextEnabled(bool allowToolbarText = false) const
        {
            return !pendingPlay && gameSession.State().IsEditing() && IdleContextEnabled(allowToolbarText);
        }

        bool IdleContextEnabled(bool allowToolbarText = false) const
        {
            return initialized && !pendingPlay && !document.Pending() && !pendingObject && !pendingHistory && !reloadRequested &&
                !gizmo.IsDragging() && !ImGui::IsMouseDown(ImGuiMouseButton_Right) && !EditingField() &&
                (!ImGui::GetIO().WantTextInput || allowToolbarText) &&
                ImGui::GetTopMostPopupModal()==nullptr && !closeRequested;
        }

        bool CommandsEnabled(bool allowToolbarText = false) const
        {
            return sceneLoaded && CommandContextEnabled(allowToolbarText);
        }

        void RequestReload()
        {
            if (editState.HasChanges()) reloadConfirmRequested=true;
            else reloadRequested=true;
        }

        void DrawMenuBar()
        {
            const bool enabled=CommandsEnabled();
            if (!ImGui::BeginMainMenuBar()) return;
            DrawFileMenu(enabled);
            DrawEditMenu(enabled);
            if (ImGui::BeginMenu("View"))
            {
                if (ImGui::MenuItem("Focus selected", "F", false, enabled && sceneViewport.Valid() && !editState.SelectedIds().empty())) focusRequested=true;
                if (ImGui::MenuItem("Console")) ImGui::SetWindowFocus("Console");
                if (ImGui::MenuItem("Game tab", nullptr, false, enabled)) focusGame=true;
                if (ImGui::MenuItem("Preview title composition", nullptr, false, enabled))
                {
                    preview=true;
                    cameraPanel.CancelDrag();
                }
                if (ImGui::MenuItem("Reset panel layout", nullptr, false, !gizmo.IsDragging())) Editor::PanelLayout::Reset();
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        void DrawFileMenu(bool enabled)
        {
            if (!ImGui::BeginMenu("File")) return;
            if (ImGui::MenuItem("New scene", nullptr, false, CommandContextEnabled())) newScenePopupRequested=true;
            DrawOpenMenu(CommandContextEnabled());
            if (ImGui::MenuItem("Save", "Ctrl+S", false, enabled)) Save();
            if (ImGui::MenuItem("Save as...", "Ctrl+Shift+S", false, enabled)) saveAsPanel.Request(document.Path());
            if (ImGui::MenuItem("Reload", nullptr, false, enabled && !document.UnsavedNew())) RequestReload();
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", nullptr, false, !gizmo.IsDragging())) closeRequested=true;
            ImGui::EndMenu();
        }

        void DrawOpenMenu(bool enabled)
        {
            if (!ImGui::BeginMenu("Open scene",enabled)) return;
            if (ImGui::MenuItem("Refresh scene list")) projectPanel.Scan(root);
            ImGui::Separator();
            for (const auto& asset : projectPanel.Catalog().Assets())
            {
                if (asset.kind!=Editor::AssetKind::Scene) continue;
                const auto path=Editor::ProjectCatalog::Text(asset.path);
                if (ImGui::MenuItem(path.c_str())) document.Request(root/asset.path,false,editState.HasChanges());
            }
            ImGui::EndMenu();
        }

        void DrawEditMenu(bool enabled)
        {
            if (!ImGui::BeginMenu("Edit")) return;
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, enabled && history.CanUndo())) pendingHistory=false;
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, enabled && history.CanRedo())) pendingHistory=true;
            ImGui::Separator();
            const bool selected=enabled && !editState.SelectedIds().empty();
            if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, selected))
                pendingObject=editState.DuplicateSelectionRequest();
            if (ImGui::MenuItem("Delete", "Delete", false, enabled && !editState.SelectedIds().empty()))
                pendingObject=editState.DeleteSelectionRequest();
            ImGui::EndMenu();
        }

        void DrawPlayToolbar()
        {
            const bool enabled=sceneLoaded && IdleContextEnabled() && !reloadConfirmRequested && !newScenePopupRequested &&
                !saveAsPanel.Requested() && !ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
            ImGui::BeginDisabled(!enabled);
            ImGui::BeginDisabled(!gameSession.State().CanPlay());
            if (ImGui::Button(gameSession.State().IsEditing() ? "Play###Play" : "Resume###Play"))
                pendingPlay=Editor::GameSession::Command::Play;
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!gameSession.State().CanPause());
            if (ImGui::Button("Pause")) pendingPlay=Editor::GameSession::Command::Pause;
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!gameSession.State().CanStop());
            if (ImGui::Button("Stop")) pendingPlay=Editor::GameSession::Command::Stop;
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!gameSession.State().CanStep());
            if (ImGui::Button("Step")) pendingPlay=Editor::GameSession::Command::Step;
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextUnformatted(gameSession.State().Label());
        }

        void DrawToolbar()
        {
            const auto flags=ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollbar;
            const float height=ImGui::GetFrameHeight()+2*ImGui::GetStyle().WindowPadding.y;
            if (ImGui::BeginViewportSideBar("Editor toolbar", ImGui::GetMainViewport(), ImGuiDir_Up, height, flags))
            {
                DrawPlayToolbar();
                ImGui::SameLine();
                const bool enabled=CommandsEnabled() &&
                    !ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
                ImGui::BeginDisabled(!enabled);
                if (ImGui::Button("Save")) Save();
                ImGui::SameLine();
                ImGui::BeginDisabled(!history.CanUndo());
                if (ImGui::Button("Undo")) pendingHistory=false;
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(!history.CanRedo());
                if (ImGui::Button("Redo")) pendingHistory=true;
                ImGui::EndDisabled();
                ImGui::EndDisabled();
                ImGui::SameLine();
                gizmo.DrawToolbar(CommandsEnabled(true));
            }
            ImGui::End();
        }

        void DrawSceneDialogs()
        {
            saveAsPanel.Draw(root,[&](const auto& path,bool overwrite) { return SaveAs(path,overwrite); },fileStatus);
            if (newScenePopupRequested) { ImGui::OpenPopup("New scene"); newScenePopupRequested=false; }
            if (ImGui::BeginPopupModal("New scene",nullptr,ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextUnformatted("Filename in Assets/Scenes (written on Save):");
                ImGui::InputText("Filename",newSceneName.data(),newSceneName.size());
                if (ImGui::Button("Create")) CreateSceneRequest();
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
                if (!fileStatus.empty()) ImGui::TextWrapped("%s",fileStatus.c_str());
                ImGui::EndPopup();
            }
            if (document.NeedsConfirmation() && !ImGui::IsPopupOpen("Switch scene with unsaved changes?"))
                ImGui::OpenPopup("Switch scene with unsaved changes?");
            DrawSceneSwitchPopup();
        }

        void CreateSceneRequest()
        {
            try
            {
                const auto path=Editor::SceneDocument::NewTarget(root,newSceneName.data());
                document.Request(path,true,editState.HasChanges());
                ImGui::CloseCurrentPopup();
            }
            catch (const std::exception& error) { fileStatus=error.what(); LogResult(false); }
        }

        void DrawSceneSwitchPopup()
        {
            if (!ImGui::BeginPopupModal("Switch scene with unsaved changes?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) return;
            ImGui::TextUnformatted("The current scene has unsaved changes.");
            if (ImGui::Button("Save and continue"))
            {
                if (Save()) { document.Confirm(); ImGui::CloseCurrentPopup(); }
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard and continue")) { document.Confirm(); ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) { document.Cancel(); ImGui::CloseCurrentPopup(); }
            if (!fileStatus.empty()) ImGui::TextWrapped("%s",fileStatus.c_str());
            ImGui::EndPopup();
        }

        void DrawReloadPopup()
        {
            if (ImGui::BeginPopupModal("Reload unsaved changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextUnformatted("The current scene has unsaved changes.");
                if (ImGui::Button("Save and reload"))
                {
                    if (Save()) { reloadRequested = true; ImGui::CloseCurrentPopup(); }
                }
                ImGui::SameLine();
                if (ImGui::Button("Discard and reload"))
                {
                    reloadRequested = true;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
                if (!fileStatus.empty()) ImGui::TextWrapped("%s", fileStatus.c_str());
                ImGui::EndPopup();
            }
        }

        const std::filesystem::path root = ContentRoot();
        SceneRuntime::SceneWorld world;
        Engine::DebugCamera camera;
        Editor::CameraPanel cameraPanel;
        Engine::RenderTexture gameTexture;
        Editor::ScenePanel gamePanel;
        UINT64 gameTextureId=0;
        std::array<UINT,2> requestedGameSize{640,480};
        bool focusGame=false;
        Engine::RenderTexture sceneTexture;
        UINT64 sceneTextureId = 0;
        std::array<UINT, 2> requestedSceneSize{640, 480};
        Editor::ScenePanel scenePanel;
        Editor::SceneViewport sceneViewport;
        Editor::EditState editState;
        Editor::GameSession gameSession;
        std::optional<Editor::GameSession::Command> pendingPlay;
        Editor::ObjectPanel objectPanel;
        Editor::ProjectPanel projectPanel;
        Editor::TransformGizmo gizmo;
        Editor::EditHistory history;
        Editor::ConsolePanel consolePanel;
        Editor::SaveAsPanel saveAsPanel;
        Engine::Camera previewCamera;
        bool preview = false;
        bool focusRequested = false;
        std::optional<bool> pendingHistory;
        std::optional<Editor::ObjectRequest> pendingObject;
        const Engine::DirectionalLight light = SceneRuntime::TitleView::Light();
        const Engine::Keyboard* keyboard = nullptr;
        double seconds = 0.0;
        bool initialized = false;
        bool sceneLoaded = false;
        bool reloadRequested = false;
        bool reloadConfirmRequested = false;
        bool closeRequested = false;
        bool closeConfirmed = false;
        std::string fileStatus;
        Editor::SceneDocument document{root / "Assets/Scenes/TitleStreet.json"};
        bool newScenePopupRequested=false;
        std::array<char,256> newSceneName{"NewScene.json"};
    };
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    StreetEditor editor;
    return editor.Run();
}
