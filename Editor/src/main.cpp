#include "CameraPanel.h"
#include "ObjectPanel.h"
#include "ProjectPanel.h"
#include "AssetChanges.h"
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
#include "UiCanvasPanel.h"
#include "AudioPreview.h"
#include "EditorFonts.h"
#include "PlaySnapshot.h"
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
            camera.SetResetPose({0,3,-10},0,0);
            camera.GetCamera().SetPerspective(DirectX::XM_PIDIV4,16.0f/9.0f,0.1f,1000);
            camera.SetMoveSpeed(8.0f);
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
            settings.title = L"WP1 エディター";
            Engine::Application application;
            return application.Run(settings, callbacks);
        }

    private:
        Engine::RenderResult Draw(Engine::DirectX12Renderer& renderer)
        {
            if (!ApplyPendingChanges(renderer) || !PrepareSceneTexture(renderer) || !PrepareGameTexture(renderer)) return Engine::RenderResult::Failed;
            if (gameSession.State().IsEditing() && !projectPanel.PreparePreview(renderer,root)) return Engine::RenderResult::Failed;
            ApplyUiEvent(renderer);
            audioPreview.Process(root);
            if (presentation && !presentation->PrepareUi(renderer,root,world.Layout(),fileStatus)) LogResult(false);
            gameSession.Update(seconds,keyboard && keyboard->IsActive() && !closeRequested);
            bool rendered = true;
            const auto result = renderer.Render(preview ? world.Layout().settings.background : std::array<float,4>{0.10f,0.11f,0.13f,1},
                [&](ID3D12GraphicsCommandList* commands, float aspect)
                {
                    static_cast<void>(aspect);
                    rendered=RenderViews(commands,renderer);
                }, [&]() { DrawUi(); });
            return rendered ? result : Engine::RenderResult::Failed;
        }

        bool RenderViews(ID3D12GraphicsCommandList* commands, const Engine::DirectX12Renderer& renderer)
        {
            if (preview)
            {
                if (presentation) presentation->Draw(commands,world,renderer.GetWidth(),renderer.GetHeight());
                return true;
            }
            bool success=projectPanel.RenderPreview(commands);
            if (sceneViewport.Valid()) success=DrawSceneTexture(commands) && success;
            if (gamePanel.Viewport().Valid()) success=DrawGameTexture(commands) && success;
            return success;
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
                ReportStatus("シーンの描画テクスチャをリサイズできませんでした。",false);
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
                ReportStatus("ゲームの描画テクスチャをリサイズできませんでした。",false);
                return gameTexture.GetResource()!=nullptr;
            }
            gameTextureId=renderer.SetSceneTexture(gameTexture.GetShaderResourceView(),1).ptr;
            return gameTextureId!=0;
        }

        bool DrawGameTexture(ID3D12GraphicsCommandList* commands)
        {
            if (!gameTexture.Begin(commands,world.Layout().settings.background)) return false;
            if (gameSession.Runtime()) gameSession.Draw(commands,gameTexture.GetWidth(),gameTexture.GetHeight());
            else if (presentation) presentation->Draw(commands,world,gameTexture.GetWidth(),gameTexture.GetHeight());
            return gameTexture.End(commands);
        }

        const SceneRuntime::SceneWorld& DisplayedWorld() const
        {
            const auto* runtime=gameSession.Runtime();
            return runtime ? runtime->World() : world;
        }

        bool DrawSceneTexture(ID3D12GraphicsCommandList* commands)
        {
            if (!sceneTexture.Begin(commands, world.Layout().settings.background)) return false;
            const auto* runtime=gameSession.Runtime();
            const auto& displayed=DisplayedWorld();
            if (presentation) presentation->Draw(commands,displayed,sceneTexture.GetWidth(),sceneTexture.GetHeight(),
                &camera.GetCamera(),runtime ? runtime->MotionSeconds() : 0,runtime ? runtime->MotionEnabled() : true);
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
                    fileStatus = "再読み込みしました。";
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
                fileStatus=document.UnsavedNew() ? "新規シーンを作成しました（未保存）。" : "シーンを開きました。";
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
                    fileStatus=redo ? "やり直しました。" : "元に戻しました。";
                    LogResult(true);
                }
                else LogResult(false);
            }
            return true;
        }

        bool ApplyObjectRequest(const Editor::ObjectRequest& request, std::string& createdId)
        {
            if (request.action==Editor::ObjectAction::Add || request.action==Editor::ObjectAction::AddEmpty)
            {
                SceneRuntime::ScenePlacement placement;
                if (request.action==Editor::ObjectAction::Add) { placement.SetModel(request.model); placement.scale={4,4,4}; }
                placement.position=request.position;
                return world.AddObject(std::move(placement),root,createdId,fileStatus);
            }
            if (request.action==Editor::ObjectAction::Duplicate)
                return editState.DuplicateObjects(world,request.ids,{4,0,0},fileStatus);
            if (request.action==Editor::ObjectAction::Delete)
                return editState.DeleteObjects(world,request.ids,fileStatus);
            if (request.action==Editor::ObjectAction::Settings) return request.settings && world.SetSettings(*request.settings,fileStatus);
            return request.components && world.SetComponents(request.id,*request.components,root,fileStatus);
        }

        bool ApplyObject(Engine::DirectX12Renderer& renderer)
        {
            if (!pendingObject) return true;
            if (!renderer.WaitForIdle()) return false;
            auto request=std::move(*pendingObject);
            pendingObject.reset();
            if (request.interaction.empty()) history.Commit();
            std::string createdId;
            const bool success=ApplyObjectRequest(request,createdId);
            if (success)
            {
                if (!createdId.empty()) editState.ObjectChanged(createdId);
                fileStatus=request.action==Editor::ObjectAction::Delete ? "削除しました。" :
                    request.action==Editor::ObjectAction::Duplicate ? "複製しました。" :
                    request.action==Editor::ObjectAction::Components ? "コンポーネントを更新しました。" :
                    request.action==Editor::ObjectAction::Settings ? "シーン設定を更新しました。" : "追加しました。";
                history.Observe(Snapshot(world.Layout().Serialize()),request.interaction);
                editState.SetChanged(document.UnsavedNew() || history.Dirty(world.Layout().Serialize()));
            }
            LogResult(success);
            return true;
        }

        bool ApplyPendingChanges(Engine::DirectX12Renderer& renderer)
        {
            if (!initialized)
            {
                if(!Editor::EditorFonts::Initialize(root,fileStatus)) {LogResult(false); return false;}
                Editor::Language::Initialize();
                const auto settingsRoot = Engine::GetDiagnosticsRoot();
                Editor::PanelLayout::Initialize(settingsRoot.empty() ? std::filesystem::path{} : settingsRoot / "Editor/layout.ini");
                sceneLoaded = world.Initialize(renderer, root, document.Path(),
                    root / "Shaders/Mesh.hlsl", &fileStatus);
                presentation=std::make_unique<SceneRuntime::ScenePresentation>();
                if (!presentation->Initialize(renderer,root,fileStatus)) { presentation.reset(); sceneLoaded=false; }
                initialized = true;
                if (sceneLoaded) history.Reset(Snapshot(world.Layout().Serialize()));
                Engine::Log::Write(sceneLoaded ? Engine::LogLevel::Info : Engine::LogLevel::Error,
                    sceneLoaded ? "エディターのシーンを読み込みました。" : fileStatus);
            }
            return ApplySceneChange(renderer) && ApplyReload(renderer) && ApplyHistory(renderer) && ApplyObject(renderer) && ApplyPlay(renderer) && ApplyAssets(renderer);
        }

        bool CanReloadAssets() const
        {
            return sceneLoaded && gameSession.State().IsEditing() && !pendingPlay &&
                !document.Pending() && !pendingObject && !pendingHistory && !gizmo.IsDragging() &&
                editState.Interaction().empty();
        }

        bool ApplyAssets(Engine::DirectX12Renderer& renderer)
        {
            assetChanges.Poll(root,seconds);
            assetReloadRequested=projectPanel.TakeAssetReloadRequest() || assetReloadRequested;
            projectPanel.SetReloadPending(assetReloadRequested || assetChanges.Pending(),assetChanges.Error());
            if (!CanReloadAssets()) return true;
            const bool changed=assetChanges.TakeReady(true);
            if (!assetReloadRequested && !changed) return true;
            assetReloadRequested=false;
            if (!renderer.WaitForIdle()) return false;
            auto candidate=std::make_unique<SceneRuntime::ScenePresentation>();
            const bool success=Editor::ValidateProjectShaders(root,fileStatus) && candidate->Initialize(renderer,root,fileStatus) &&
                candidate->PrepareUi(renderer,root,world.Layout(),fileStatus) && world.ReloadAssets(renderer,root,root/"Shaders/Mesh.hlsl",fileStatus);
            if (success)
            {
                presentation=std::move(candidate);
                projectPanel.Scan(root);
                fileStatus="アセットを再読み込みしました。未保存のシーンと履歴を維持しています。";
            }
            LogResult(success);
            return true;
        }

        bool ApplyPlay(Engine::DirectX12Renderer& renderer)
        {
            if (!pendingPlay) return true;
            const auto command=*pendingPlay;
            pendingPlay.reset();
            if (command==Editor::GameSession::Command::Pause)
            {
                if (gameSession.Pause()) ReportStatus("ゲームを一時停止しました。",true);
                return true;
            }
            if (command==Editor::GameSession::Command::Step)
            {
                if (gameSession.Step()) ReportStatus("1フレーム（1/60秒）進めました。",true);
                return true;
            }
            if (!renderer.WaitForIdle()) return false;
            if (command==Editor::GameSession::Command::Stop) StopGame();
            else StartGame(renderer);
            return true;
        }

        void StartGame(const Engine::DirectX12Renderer& renderer)
        {
            std::optional<Editor::PlaySnapshot> captured;
            Editor::AudioPreview::StopRequest();
            audioPreview.Process(root);
            if (gameSession.State().IsEditing())
            {
                history.Commit();
                captured.emplace(world,editState,history,document);
            }
            if (!gameSession.Play(renderer,root,world.Layout(),fileStatus)) { LogResult(false); return; }
            if (captured) playSnapshot=std::move(captured);
            focusGame=true;
            cameraPanel.CancelDrag();
            ReportStatus("再生を開始しました。",true);
        }

        void StopGame()
        {
            if (!gameSession.State().CanStop()) return;
            if (playSnapshot && !playSnapshot->Restore(world,root,editState,history,document,fileStatus))
            {
                gameSession.Pause();
                LogResult(false);
                return;
            }
            if (gameSession.Stop())
            {
                playSnapshot.reset();
                ReportStatus("編集へ戻りました。",true);
            }
        }

        bool Save()
        {
            if (!sceneLoaded) { fileStatus = "保存するシーンが読み込まれていません。"; LogResult(false); return false; }
            try
            {
                document.Save(world.Layout());
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
                SavedSuccessfully("別名で保存しました："+Editor::ProjectCatalog::Text(document.Path().filename()));
                return true;
            }
            catch (const std::exception& error)
            {
                fileStatus=std::string("別名保存に失敗しました：")+error.what();
                LogResult(false);
                return false;
            }
        }

        void DrawUi()
        {
            editState.BeginFrame();
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
            gamePanel.Begin(gameTextureId,"ゲーム###Game");
            requestedGameSize=gamePanel.RequestedSize();
            uiCanvasPanel.Draw(world,editState,gamePanel.Viewport(),SceneEditingEnabled());
            UpdateGamePointer();
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
            if (!preview) Editor::SceneSelection::Draw(DisplayedWorld(), camera.GetCamera(), editState, sceneViewport);
            Editor::ScenePanel::End();
        }

        void ApplyUiEvent(Engine::DirectX12Renderer& renderer)
        {
            if(!pendingUiEvent) return;
            if(!renderer.WaitForIdle()) return;
            const auto event=std::move(*pendingUiEvent); pendingUiEvent.reset();
            if(event.action=="quit") { StopGame(); return; }
            if(event.action!="loadScene") return;
            try {
                const auto layout=SceneRuntime::SceneLayout::Load(root/std::filesystem::path(event.target));
                if(!gameSession.LoadScene(renderer,root,layout,fileStatus)) LogResult(false);
            } catch(const std::exception& e) { ReportStatus(e.what(),false); }
        }

        void UpdateGamePointer()
        {
            auto* runtime=gameSession.Runtime(); if(!runtime) return;
            if(!gameSession.State().CanPause() || !keyboard || !keyboard->IsActive()) {runtime->Ui().pressed.clear(); runtime->Ui().hovered.clear(); return;}
            const auto& v=gamePanel.Viewport(); if(!v.Valid()) return;
            auto& ui=runtime->Ui(); const auto mouse=ImGui::GetIO().MousePos;
            ui.hovered=v.Contains(mouse.x,mouse.y) && gamePanel.Hovered()?SceneRuntime::SceneUi::Hit(runtime->World().Layout(),static_cast<unsigned int>(v.width),static_cast<unsigned int>(v.height),mouse.x-v.x,mouse.y-v.y,ui):std::string{};
            if(ImGui::IsMouseClicked(ImGuiMouseButton_Left)) ui.pressed=ui.hovered;
            if(ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                if(!ui.pressed.empty() && ui.pressed==ui.hovered) pendingUiEvent=runtime->Click(ui.pressed);
                ui.pressed.clear();
            }
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
                    camera.GetCamera().SetAspectRatio(sceneViewport.Aspect());
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
            if (!ImGui::IsPopupOpen("未保存の変更があります：終了###Exit with unsaved changes?")) ImGui::OpenPopup("未保存の変更があります：終了###Exit with unsaved changes?");
            if (ImGui::BeginPopupModal("未保存の変更があります：終了###Exit with unsaved changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextUnformatted("現在のシーンに未保存の変更があります。");
                if (ImGui::Button("保存して終了###Save and exit"))
                {
                    if (Save()) { closeConfirmed = true; ImGui::CloseCurrentPopup(); }
                }
                ImGui::SameLine();
                if (ImGui::Button("保存せず終了###Exit without saving"))
                {
                    closeConfirmed = true;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("キャンセル###Cancel"))
                {
                    closeRequested = false;
                    ImGui::CloseCurrentPopup();
                }
                if (!audioPreview.error.empty()) ImGui::TextWrapped("音声：%s",audioPreview.error.c_str());
                if (!fileStatus.empty()) ImGui::TextWrapped("%s", fileStatus.c_str());
                ImGui::EndPopup();
            }
        }

        void DrawPreview()
        {
            if (keyboard && keyboard->IsActive() && ImGui::IsKeyPressed(ImGuiKey_Escape,false)) preview=false;
            ImGui::SetNextWindowPos(ImVec2(20,20),ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.8f);
            if (ImGui::Begin("ゲーム画面の確認###Game composition", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextUnformatted("シーンのカメラ・照明：未保存の編集を含む現在の配置");
                ImGui::TextUnformatted("シーンのカメラ・空・照明・パーティクル：現在の編集中の配置");
                if (ImGui::Button("編集へ戻る（Escape）###Back to editing (Escape)")) preview=false;
            }
            ImGui::End();
        }

        void UpdateCamera()
        {
            if (keyboard) cameraPanel.Draw(camera, *keyboard, seconds, sceneViewport, scenePanel.Hovered(), !gizmo.IsDragging() && !ImGui::GetDragDropPayload());
            if (sceneViewport.Valid())
                camera.GetCamera().SetAspectRatio(sceneViewport.Aspect());
        }

        bool CanFocus() const
        {
            return editState.InspectedAsset().empty() && !pendingPlay && sceneViewport.Valid() && !document.Pending() && sceneLoaded && keyboard && keyboard->IsActive() && !gizmo.IsDragging() &&
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
                    else fileStatus="選択対象がカメラの範囲に収まらないか、対象のオブジェクトがありません。";
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
            const bool sceneInput=SceneEditingEnabled() && !ImGui::GetDragDropPayload();
            gizmo.UpdateAndDraw(world, camera.GetCamera(), editState, sceneViewport,
                sceneInput && editState.InspectedAsset().empty() && (scenePanel.Hovered() || gizmo.IsDragging()));
            Editor::SceneSelection::Update(world, camera.GetCamera(), editState, sceneViewport,
                sceneInput && !gizmo.ConsumesMouse() && scenePanel.Hovered());
            DirectX::XMFLOAT4X4 viewInverse;
            DirectX::XMStoreFloat4x4(&viewInverse, DirectX::XMMatrixInverse(nullptr, camera.GetViewMatrix()));
            const auto& eye = camera.GetPosition();
            const std::array<float, 3> suggested{ eye[0] + viewInverse._31 * 8.0f, 0.08f,
                eye[2] + viewInverse._33 * 8.0f };
            projectPanel.Draw(editState, suggested, EditWidgetsEnabled() && !pendingObject && !pendingHistory);
            objectPanel.Draw(world, editState, EditWidgetsEnabled(),&projectPanel.Catalog());
            projectPanel.DrawInspector(editState,EditWidgetsEnabled());
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
            else if (ImGui::IsKeyPressed(ImGuiKey_Delete,false) && editState.InspectedAsset().empty() && !editState.SelectedIds().empty())
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
            else if (ImGui::IsKeyPressed(ImGuiKey_D,false) && editState.InspectedAsset().empty() && !editState.SelectedIds().empty())
                pendingObject=editState.DuplicateSelectionRequest();
        }

        void DrawCommands()
        {
            Editor::PanelLayout::Place(Editor::PanelLayout::Panel::Commands);
            if (ImGui::Begin("エディター###Street Editor"))
            {
                ImGui::TextWrapped("シーン：%s",Editor::ProjectCatalog::Text(document.Path().filename()).c_str());
                ImGui::Text("オブジェクト数：%zu", world.Layout().objects.size());
                ImGui::Text("ゲーム：%s / %.2f秒",gameSession.State().Label(),gameSession.State().Elapsed());
                if (!SceneRuntime::SceneView::CameraObject(world.Layout()) &&
                    std::any_of(world.Layout().objects.begin(),world.Layout().objects.end(),[](const auto& p){return p.meshRenderer || p.particleEmitter;}))
                    ImGui::TextWrapped("有効なゲームカメラがありません。空のオブジェクトを作成し、カメラを追加してください。");
                ImGui::TextUnformatted(editState.HasChanges() ? "未保存の変更あり" : "保存済み・変更なし");

                if (ImGui::CollapsingHeader("ヘルプ・コンテンツ###Help / Content"))
                {
                    ImGui::TextWrapped("Ctrl+S：保存／Ctrl+D：複製／Delete：削除／1・2・3：移動・回転・拡縮／F：フォーカス");
                    ImGui::TextWrapped("再生で現在のシーンを実行します。一時停止で動きを止め、コマ送りで1フレーム進め、停止で編集状態に戻ります。");
                    ImGui::TextWrapped("コンテンツ：%s",Editor::ProjectCatalog::Text(root).c_str());
                }
                if (!audioPreview.error.empty()) ImGui::TextWrapped("音声：%s",audioPreview.error.c_str());
                if (!fileStatus.empty()) ImGui::TextWrapped("%s", fileStatus.c_str());
                if (!Editor::PanelLayout::error.empty()) ImGui::TextWrapped("パネル配置：%s", Editor::PanelLayout::error.c_str());
                if (reloadConfirmRequested)
                {
                    reloadConfirmRequested=false;
                    ImGui::OpenPopup("未保存の変更があります：再読み込み###Reload unsaved changes?");
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
            if (ImGui::BeginMenu("表示###View"))
            {
                if (ImGui::MenuItem("選択対象にフォーカス###Focus selected", "F", false, enabled && editState.InspectedAsset().empty() && sceneViewport.Valid() && !editState.SelectedIds().empty())) focusRequested=true;
                if (ImGui::MenuItem("コンソール###Console")) ImGui::SetWindowFocus("コンソール###Console");
                if (ImGui::MenuItem("ゲームタブ###Game tab", nullptr, false, enabled)) focusGame=true;
                if (ImGui::MenuItem("ゲーム画面を確認###Preview Game composition", nullptr, false, enabled))
                {
                    preview=true;
                    cameraPanel.CancelDrag();
                }
                if (ImGui::MenuItem("パネル配置をリセット###Reset panel layout", nullptr, false, !gizmo.IsDragging())) Editor::PanelLayout::Reset();
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        void DrawFileMenu(bool enabled)
        {
            if (!ImGui::BeginMenu("ファイル###File")) return;
            if (ImGui::MenuItem("新規シーン###New scene", nullptr, false, CommandContextEnabled())) newScenePopupRequested=true;
            DrawOpenMenu(CommandContextEnabled());
            if (ImGui::MenuItem("保存###Save", "Ctrl+S", false, enabled)) Save();
            if (ImGui::MenuItem("名前を付けて保存…###Save as...", "Ctrl+Shift+S", false, enabled)) saveAsPanel.Request(document.Path());
            if (ImGui::MenuItem("再読み込み###Reload", nullptr, false, enabled && !document.UnsavedNew())) RequestReload();
            ImGui::Separator();
            if (ImGui::MenuItem("終了###Exit", nullptr, false, !gizmo.IsDragging())) closeRequested=true;
            ImGui::EndMenu();
        }

        void DrawOpenMenu(bool enabled)
        {
            if (!ImGui::BeginMenu("シーンを開く###Open scene",enabled)) return;
            if (ImGui::MenuItem("シーン一覧を更新###Refresh scene list")) projectPanel.Scan(root);
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
            if (!ImGui::BeginMenu("編集###Edit")) return;
            if (ImGui::MenuItem("元に戻す###Undo", "Ctrl+Z", false, enabled && history.CanUndo())) pendingHistory=false;
            if (ImGui::MenuItem("やり直す###Redo", "Ctrl+Y", false, enabled && history.CanRedo())) pendingHistory=true;
            ImGui::Separator();
            const bool selected=enabled && editState.InspectedAsset().empty() && !editState.SelectedIds().empty();
            if (ImGui::MenuItem("複製###Duplicate", "Ctrl+D", false, selected))
                pendingObject=editState.DuplicateSelectionRequest();
            if (ImGui::MenuItem("削除###Delete", "Delete", false, selected))
                pendingObject=editState.DeleteSelectionRequest();
            ImGui::EndMenu();
        }

        void DrawPlayToolbar()
        {
            const bool enabled=sceneLoaded && IdleContextEnabled() && !reloadConfirmRequested && !newScenePopupRequested &&
                !saveAsPanel.Requested() && !ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
            ImGui::BeginDisabled(!enabled);
            ImGui::BeginDisabled(!gameSession.State().CanPlay());
            if (ImGui::Button(gameSession.State().IsEditing() ? "再生###Play" : "再開###Play"))
                pendingPlay=Editor::GameSession::Command::Play;
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!gameSession.State().CanPause());
            if (ImGui::Button("一時停止###Pause")) pendingPlay=Editor::GameSession::Command::Pause;
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!gameSession.State().CanStop());
            if (ImGui::Button("停止###Stop")) pendingPlay=Editor::GameSession::Command::Stop;
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!gameSession.State().CanStep());
            if (ImGui::Button("コマ送り###Step")) pendingPlay=Editor::GameSession::Command::Step;
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
                if (ImGui::Button("保存###Save")) Save();
                ImGui::SameLine();
                ImGui::BeginDisabled(!history.CanUndo());
                if (ImGui::Button("元に戻す###Undo")) pendingHistory=false;
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(!history.CanRedo());
                if (ImGui::Button("やり直す###Redo")) pendingHistory=true;
                ImGui::EndDisabled();
                ImGui::EndDisabled();
                ImGui::SameLine();
                gizmo.DrawToolbar(CommandsEnabled(true) && editState.InspectedAsset().empty());
            }
            ImGui::End();
        }

        void DrawSceneDialogs()
        {
            saveAsPanel.Draw(root,[&](const auto& path,bool overwrite) { return SaveAs(path,overwrite); },fileStatus);
            if (newScenePopupRequested) { ImGui::OpenPopup("新規シーン###New scene"); newScenePopupRequested=false; }
            if (ImGui::BeginPopupModal("新規シーン###New scene",nullptr,ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextUnformatted("Assets/Scenes内のファイル名（保存時に作成）：");
                ImGui::InputText("ファイル名###Filename",newSceneName.data(),newSceneName.size());
                if (ImGui::Button("作成###Create")) CreateSceneRequest();
                ImGui::SameLine();
                if (ImGui::Button("キャンセル###Cancel")) ImGui::CloseCurrentPopup();
                if (!audioPreview.error.empty()) ImGui::TextWrapped("音声：%s",audioPreview.error.c_str());
                if (!fileStatus.empty()) ImGui::TextWrapped("%s",fileStatus.c_str());
                ImGui::EndPopup();
            }
            if (document.NeedsConfirmation() && !ImGui::IsPopupOpen("未保存の変更があります：シーン切り替え###Switch scene with unsaved changes?"))
                ImGui::OpenPopup("未保存の変更があります：シーン切り替え###Switch scene with unsaved changes?");
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
            if (!ImGui::BeginPopupModal("未保存の変更があります：シーン切り替え###Switch scene with unsaved changes?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) return;
            ImGui::TextUnformatted("現在のシーンに未保存の変更があります。");
            if (ImGui::Button("保存して続行###Save and continue"))
            {
                if (Save()) { document.Confirm(); ImGui::CloseCurrentPopup(); }
            }
            ImGui::SameLine();
            if (ImGui::Button("変更を破棄して続行###Discard and continue")) { document.Confirm(); ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (ImGui::Button("キャンセル###Cancel")) { document.Cancel(); ImGui::CloseCurrentPopup(); }
            if (!fileStatus.empty()) ImGui::TextWrapped("%s",fileStatus.c_str());
            ImGui::EndPopup();
        }

        void DrawReloadPopup()
        {
            if (ImGui::BeginPopupModal("未保存の変更があります：再読み込み###Reload unsaved changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextUnformatted("現在のシーンに未保存の変更があります。");
                if (ImGui::Button("保存して再読み込み###Save and reload"))
                {
                    if (Save()) { reloadRequested = true; ImGui::CloseCurrentPopup(); }
                }
                ImGui::SameLine();
                if (ImGui::Button("変更を破棄して再読み込み###Discard and reload"))
                {
                    reloadRequested = true;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("キャンセル###Cancel")) ImGui::CloseCurrentPopup();
                if (!audioPreview.error.empty()) ImGui::TextWrapped("音声：%s",audioPreview.error.c_str());
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
        std::optional<SceneRuntime::UiEvent> pendingUiEvent;
        Editor::AudioPreview audioPreview;
        Editor::UiCanvasPanel uiCanvasPanel;
        Editor::GameSession gameSession;
        std::optional<Editor::PlaySnapshot> playSnapshot;
        std::optional<Editor::GameSession::Command> pendingPlay;
        Editor::ObjectPanel objectPanel;
        Editor::ProjectPanel projectPanel;
        Editor::AssetChanges assetChanges;
        bool assetReloadRequested=false;
        Editor::TransformGizmo gizmo;
        Editor::EditHistory history;
        Editor::ConsolePanel consolePanel;
        Editor::SaveAsPanel saveAsPanel;
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
