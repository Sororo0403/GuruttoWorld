#include "CameraPanel.h"
#include "ObjectPanel.h"
#include "SceneSelection.h"
#include "TransformGizmo.h"
#include "EditHistory.h"
#include "FocusSelection.h"
#include "PanelLayout.h"
#include <SceneRuntime/TitleView.h>
#include <SceneRuntime/SceneWorld.h>
#include <Engine/Core/Application.h>
#include <Engine/Core/Log.h>
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
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    const auto root = ContentRoot();
    if (root.empty()) return 1;
    SceneRuntime::SceneWorld world;
    Engine::DebugCamera camera;
    camera.SetResetPose({ -0.8f, 2.8f, -7.0f }, 0.03f, 0.09f);
    camera.SetMoveSpeed(8.0f);
    Editor::CameraPanel cameraPanel;
    Editor::ObjectPanel objectPanel;
    Editor::TransformGizmo gizmo;
    Editor::EditHistory history;
    Engine::Camera previewCamera;
    SceneRuntime::TitleView::SetHome(previewCamera);
    bool preview = false;
    bool focusRequested = false;
    std::optional<bool> pendingHistory;
    objectPanel.ScanModels(root);
    std::optional<Editor::ObjectRequest> pendingObject;
    const auto light = SceneRuntime::TitleView::Light();
    const Engine::Keyboard* keyboard = nullptr;
    double seconds = 0.0;
    bool initialized = false;
    bool sceneLoaded = false;
    bool reloadRequested = false;
    bool closeRequested = false;
    bool closeConfirmed = false;
    std::string fileStatus;
    const auto layoutPath = root / "Assets/Scenes/TitleStreet.json";
    const auto save = [&]()
    {
        if (!sceneLoaded) { fileStatus = "No scene is loaded to save."; return false; }
        try
        {
            world.Layout().Save(layoutPath);
            history.Saved(world.Layout().Serialize());
            objectPanel.MarkSaved();
            fileStatus = "Saved.";
            return true;
        }
        catch (const std::exception& error)
        {
            fileStatus = std::string("Save failed: ") + error.what();
            return false;
        }
    };
    Engine::ApplicationCallbacks callbacks;
    callbacks.closeRequested = [&]() { closeRequested = true; };
    callbacks.shouldClose = [&]() { return closeConfirmed; };
    callbacks.update = [&](double dt, const Engine::Keyboard& input)
    {
        keyboard = &input;
        seconds = dt;
        if (!input.IsActive()) cameraPanel.CancelDrag();
    };
    callbacks.draw = [&](Engine::DirectX12Renderer& renderer)
    {
        if (!initialized)
        {
            sceneLoaded = world.Initialize(renderer, root, layoutPath,
                root / "Shaders/TitleMesh.hlsl", &fileStatus);
            initialized = true;
            if (sceneLoaded) history.Reset({world.Layout().Serialize(), objectPanel.SelectedId()});
        }
        if (reloadRequested)
        {
            reloadRequested = false;
            if (!renderer.WaitForIdle()) return Engine::RenderResult::Failed;
            if (world.Reload(root, layoutPath, fileStatus))
            {
                sceneLoaded = true;
                objectPanel.Reloaded();
                history.Reset({world.Layout().Serialize(), objectPanel.SelectedId()});
                fileStatus = "Reloaded.";
            }
        }
        if (pendingHistory)
        {
            if (!renderer.WaitForIdle()) return Engine::RenderResult::Failed;
            const bool redo=*pendingHistory;
            pendingHistory.reset();
            const auto target=history.Target(redo);
            if (world.ReplaceLayout(SceneRuntime::SceneLayout::Parse(target.json), root, fileStatus))
            {
                history.Applied(redo);
                objectPanel.Select(target.selection);
                objectPanel.SetChanged(history.Dirty(target.json));
                fileStatus=redo ? "Redone." : "Undone.";
            }
        }
        if (pendingObject)
        {
            if (!renderer.WaitForIdle()) return Engine::RenderResult::Failed;
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
                success = world.DuplicateObject(request.id, { 4, 0, 0 }, createdId, fileStatus);
            else
            {
                success = world.RemoveObject(request.id);
                fileStatus = success ? "" : "Object no longer exists.";
            }
            if (success)
            {
                objectPanel.ObjectChanged(createdId);
                fileStatus = request.action == Editor::ObjectAction::Delete ? "Deleted." :
                    request.action == Editor::ObjectAction::Duplicate ? "Duplicated." : "Added.";
                history.Observe({world.Layout().Serialize(), objectPanel.SelectedId()}, false);
            }
        }
        return renderer.Render({ 0.66f, 0.79f, 0.83f, 1.0f }, [&](ID3D12GraphicsCommandList* commands, float aspect)
        {
            auto& view = preview ? previewCamera : camera.GetCamera();
            SceneRuntime::TitleView::SetProjection(view, aspect);
            world.Draw(commands, view, light);
        }, [&]()
        {
            Editor::TransformGizmo::BeginFrame();
            Editor::PanelLayout::BeginFrame();
            if (closeRequested)
            {
                cameraPanel.CancelDrag();
                if (!sceneLoaded || !history.Dirty(world.Layout().Serialize()))
                {
                    closeConfirmed = true;
                    return;
                }
                history.Observe({world.Layout().Serialize(), objectPanel.SelectedId()}, false);
                // 編集中のギズモを止め、確認中は配置を変更しません。
                gizmo.UpdateAndDraw(world, camera.GetCamera(), objectPanel, false);
                if (!ImGui::IsPopupOpen("Exit with unsaved changes?")) ImGui::OpenPopup("Exit with unsaved changes?");
                if (ImGui::BeginPopupModal("Exit with unsaved changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
                {
                    ImGui::TextUnformatted("The current scene has unsaved changes.");
                    if (ImGui::Button("Save and exit"))
                    {
                        if (save()) { closeConfirmed = true; ImGui::CloseCurrentPopup(); }
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
                return;
            }
            if (preview)
            {
                ImGui::SetNextWindowPos(ImVec2(20,20),ImGuiCond_Always);
                ImGui::SetNextWindowBgAlpha(0.8f);
                if (ImGui::Begin("Title composition", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
                {
                    ImGui::TextUnformatted("Title camera / lighting - current layout (including unsaved edits)");
                    ImGui::TextUnformatted("Fixed view; sky, particles and title UI are excluded.");
                    if (ImGui::Button("Back to editing (Escape)") ||
                        (keyboard && keyboard->IsActive() && ImGui::IsKeyPressed(ImGuiKey_Escape,false))) preview=false;
                }
                ImGui::End();
                return;
            }
            if (keyboard) cameraPanel.Draw(camera, *keyboard, seconds, !gizmo.IsDragging());
            const auto display = ImGui::GetIO().DisplaySize;
            if (display.x > 0 && display.y > 0)
            {
                const float aspect = display.x / display.y;
                SceneRuntime::TitleView::SetProjection(camera.GetCamera(), aspect);
            }
            const bool canFocus=sceneLoaded && keyboard && keyboard->IsActive() && !gizmo.IsDragging() &&
                !pendingObject && !pendingHistory && !reloadRequested &&
                !ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) &&
                !ImGui::IsMouseDown(ImGuiMouseButton_Right);
            if (canFocus && !ImGui::IsAnyItemActive() && !ImGui::GetIO().WantTextInput &&
                !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyAlt)
            {
                if (ImGui::IsKeyPressed(ImGuiKey_1,false)) gizmo.SetMode(Editor::TransformGizmo::Mode::Move);
                if (ImGui::IsKeyPressed(ImGuiKey_2,false)) gizmo.SetMode(Editor::TransformGizmo::Mode::Rotate);
                if (ImGui::IsKeyPressed(ImGuiKey_3,false)) gizmo.SetMode(Editor::TransformGizmo::Mode::Scale);
            }
            if (focusRequested || (canFocus && !ImGui::IsAnyItemActive() && !ImGui::GetIO().WantTextInput &&
                !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyAlt && ImGui::IsKeyPressed(ImGuiKey_F,false)))
            {
                focusRequested=false;
                if (canFocus)
                {
                    std::array<std::array<float,3>,8> corners;
                    if (world.WorldBounds(objectPanel.SelectedId(),corners))
                    {
                        const auto position=Editor::FocusPosition(corners,camera.GetCamera());
                        if (position)
                        {
                            cameraPanel.CancelDrag();
                            camera.GetCamera().SetPosition(*position);
                        }
                        else fileStatus="Selected object is too large to fit within the camera range.";
                    }
                }
            }
            gizmo.UpdateAndDraw(world, camera.GetCamera(), objectPanel,
                sceneLoaded && keyboard && keyboard->IsActive() && !reloadRequested && !pendingObject);
            Editor::SceneSelection::Update(world, camera.GetCamera(), objectPanel,
                sceneLoaded && keyboard && keyboard->IsActive() && !reloadRequested && !pendingObject && !gizmo.ConsumesMouse());
            DirectX::XMFLOAT4X4 viewInverse;
            DirectX::XMStoreFloat4x4(&viewInverse, DirectX::XMMatrixInverse(nullptr, camera.GetViewMatrix()));
            const auto& eye = camera.GetPosition();
            const std::array<float, 3> suggested{ eye[0] + viewInverse._31 * 8.0f, 0.08f,
                eye[2] + viewInverse._33 * 8.0f };
            objectPanel.Draw(world, suggested, sceneLoaded && !reloadRequested && !gizmo.IsDragging());
            if (auto request = objectPanel.TakeRequest()) pendingObject = std::move(request);
            if (sceneLoaded)
            {
                const auto json=world.Layout().Serialize();
                history.Observe({json, objectPanel.SelectedId()}, gizmo.IsDragging() || ImGui::IsAnyItemActive());
                objectPanel.SetChanged(history.Dirty(json));
            }
            const bool historyEnabled=sceneLoaded && !pendingObject && !pendingHistory && !reloadRequested &&
                !gizmo.IsDragging() && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
            const bool shortcutsEnabled=historyEnabled && keyboard && keyboard->IsActive() &&
                !ImGui::IsAnyItemActive() && !ImGui::GetIO().WantTextInput && !ImGui::GetIO().KeyAlt &&
                !ImGui::IsMouseDown(ImGuiMouseButton_Right);
            if (shortcutsEnabled && ImGui::GetIO().KeyCtrl)
            {
                if (ImGui::IsKeyPressed(ImGuiKey_Z, false))
                {
                    const bool redo=ImGui::GetIO().KeyShift;
                    if (redo ? history.CanRedo() : history.CanUndo()) pendingHistory=redo;
                }
                else if (ImGui::IsKeyPressed(ImGuiKey_Y, false) && history.CanRedo()) pendingHistory=true;
                else if (ImGui::IsKeyPressed(ImGuiKey_S,false)) save();
                else if (ImGui::IsKeyPressed(ImGuiKey_D,false) && !objectPanel.SelectedId().empty())
                    pendingObject=Editor::ObjectRequest{Editor::ObjectAction::Duplicate,objectPanel.SelectedId(),{}, {}};
            }
            else if (shortcutsEnabled && ImGui::IsKeyPressed(ImGuiKey_Delete,false) && !objectPanel.SelectedId().empty())
                pendingObject=Editor::ObjectRequest{Editor::ObjectAction::Delete,objectPanel.SelectedId(),{}, {}};
            Editor::PanelLayout::Place(Editor::PanelLayout::Panel::Commands);
            if (ImGui::Begin("Street Editor"))
            {
                ImGui::Text("Objects: %zu", world.Layout().objects.size());
                ImGui::TextUnformatted(objectPanel.HasChanges() ? "Unsaved changes" : "Saved / unchanged");
                ImGui::BeginDisabled(!canFocus || objectPanel.SelectedId().empty());
                if (ImGui::Button("Focus selected (F)")) focusRequested=true;
                ImGui::EndDisabled();
                ImGui::BeginDisabled(!historyEnabled);
                if (ImGui::Button("Preview title composition"))
                {
                    preview=true;
                    cameraPanel.CancelDrag();
                }
                ImGui::EndDisabled();
                ImGui::BeginDisabled(!historyEnabled || !history.CanUndo());
                if (ImGui::Button("Undo (Ctrl+Z)")) pendingHistory=false;
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(!historyEnabled || !history.CanRedo());
                if (ImGui::Button("Redo (Ctrl+Y)")) pendingHistory=true;
                ImGui::EndDisabled();
                ImGui::BeginDisabled(!sceneLoaded || pendingObject.has_value() || pendingHistory.has_value() || gizmo.IsDragging());
                if (ImGui::Button("Save")) save();
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(pendingObject.has_value() || pendingHistory.has_value() || gizmo.IsDragging());
                if (ImGui::Button("Reload"))
                {
                    if (objectPanel.HasChanges()) ImGui::OpenPopup("Reload unsaved changes?");
                    else reloadRequested = true;
                }
                ImGui::EndDisabled();
                if (ImGui::Button("Reset panel layout")) Editor::PanelLayout::Reset();
                if (ImGui::CollapsingHeader("Help / Content"))
                {
                    ImGui::TextWrapped("Ctrl+S: Save / Ctrl+D: Duplicate / Delete: Remove / 1,2,3: Move,Rotate,Scale / F: Focus");
                    ImGui::TextWrapped("Content: %s",root.string().c_str());
                }
                if (!fileStatus.empty()) ImGui::TextWrapped("%s", fileStatus.c_str());
                if (ImGui::BeginPopupModal("Reload unsaved changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
                {
                    ImGui::TextUnformatted("The current scene has unsaved changes.");
                    if (ImGui::Button("Save and reload"))
                    {
                        if (save()) { reloadRequested = true; ImGui::CloseCurrentPopup(); }
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
            ImGui::End();
            if (!preview) Editor::SceneSelection::Draw(world, camera.GetCamera(), objectPanel);
        });
    };
    Engine::ApplicationSettings settings;
    settings.title = L"WP1 Street Editor";
    Engine::Application application;
    return application.Run(settings, callbacks);
}
