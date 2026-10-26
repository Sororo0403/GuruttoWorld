#include <SceneRuntime/Scene/SceneDocumentData.h>
#include <SceneRuntime/Scene/SceneTransforms.h>
#include "../../../Editor/src/Editing/EditState.h"
#include "../../../Editor/src/Editing/EditHistory.h"
#include <iostream>
#include <limits>

#if defined(__d3d12_h__) || defined(_WINDOWS_)
#error Document editing must not include platform or rendering APIs
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
        std::cout << "PASS: " << name << '\n';
    }

    DirectX::XMFLOAT4X4 World(const SceneLayout& layout, const std::string& id)
    {
        std::vector<DirectX::XMFLOAT4X4> matrices;
        std::string error;
        if (!SceneTransforms::Resolve(layout, matrices, error))
            throw std::runtime_error(error);
        for (size_t index = 0; index < layout.objects.size(); ++index)
            if (layout.objects[index].id == id)
                return matrices[index];
        throw std::runtime_error("Missing test object");
    }

    void Validate()
    {
        ScenePlacement root, child, peer;
        root.id = "root";
        root.name = "Root";
        root.position = {2, 0, 0};
        root.meshRenderer = MeshRendererComponent{};
        root.meshRenderer->model = "Assets/Models/Missing.glb";
        child.id = "child";
        child.name = "Child";
        child.parentId = root.id;
        child.position = {1, 0, 0};
        peer.id = "peer";
        peer.name = "Peer";
        peer.position = {-3, 0, 0};
        SceneLayout layout;
        layout.objects = {root, child, peer};
        layout.sceneObjects["base"] = {root.id, child.id, peer.id};
        SceneDocumentData document(layout);
        Editor::EditState editor;
        std::string error;
        Require(document.Read().Object(root.id)->Model() == root.Model(), "headless-missing-asset-remains-editable");
        Require(editor.SetLocalTransform(document, root.id, {4, 0, 0}, {}, {1, 1, 1}), "headless-editor-transform");
        editor.Select(root.id);
        editor.Select(child.id, true);
        Require(editor.TranslateSelectionWorld(document, {1, 2, 0}) &&
                    document.Read().Transform(child.id)->position == child.position,
                "headless-selection-moves-child-once");
        const auto beforeParent = World(document.Layout(), child.id);
        Require(editor.SetParent(document, child.id, peer.id, error) &&
                    SceneTransforms::Matches(beforeParent, World(document.Layout(), child.id)),
                "headless-parent-preserves-world-pose");
        document.TakeChanges();
        Require(editor.Rename(document, root.id, "Renamed"), "headless-editor-name");
        const auto stable = document.Layout().Serialize();
        Require(!editor.SetParent(document, peer.id, child.id, error) && document.Layout().Serialize() == stable,
                "headless-cycle-retains-document");
        Require(!document.TakeChanges().Empty() && document.TakeChanges().Empty(),
                "headless-failure-retains-pending-changes");
        auto components = *document.Read().Object(root.id);
        components.meshRenderer.reset();
        Require(document.Edit(ComponentsEdit{{components}}, error).succeeded &&
                    !document.Read().Object(root.id)->meshRenderer,
                "headless-repair-missing-asset");
        const auto repaired = document.Layout().Serialize();
        Require(SceneLayout::Parse(repaired).Serialize() == repaired, "headless-save-candidate-roundtrip");
        components.pointLight = PointLightComponent{};
        auto missing = components;
        missing.id = "missing";
        Require(!document.Edit(ComponentsEdit{{components, missing}}, error).succeeded &&
                    document.Layout().Serialize() == repaired,
                "headless-component-batch-is-atomic");
        Require(editor.SetParent(document, child.id, root.id, error), "headless-restore-hierarchy");
        Require(editor.DuplicateObjects(document, {root.id, child.id}, {10, 0, 0}, error), "headless-editor-duplicate");
        const auto created = editor.SelectedIds();
        Require(created.size() == 2 && document.Read().Object(created[1])->parentId == created[0],
                "headless-duplicate-remaps-hierarchy");
        Require(document.Layout().sceneObjects.at("base").size() == 5, "headless-duplicate-keeps-scene-ownership");
        const auto childWorld = World(document.Layout(), child.id);
        Require(editor.DeleteObjects(document, {root.id}, error) &&
                    document.Read().Object(child.id)->parentId.empty() &&
                    SceneTransforms::Matches(childWorld, World(document.Layout(), child.id)),
                "headless-delete-detaches-surviving-child");

        Editor::EditHistory history;
        const auto initial = document.Layout().Serialize();
        history.Reset({initial, child.id, {child.id}});
        Require(editor.SetLocalTransform(document, child.id, {20, 0, 0}, {}, {1, 1, 1}), "headless-history-first-edit");
        history.Observe(document.Layout().Serialize(), child.id, {child.id}, "drag");
        Require(editor.SetLocalTransform(document, child.id, {30, 0, 0}, {}, {1, 1, 1}),
                "headless-history-second-edit");
        const auto final = document.Layout().Serialize();
        history.Observe(final, child.id, {child.id}, "drag");
        history.Commit();
        Require(history.CanUndo() &&
                    document.Edit(ReplaceDocumentEdit{SceneLayout::Parse(history.Target(false).json)}, error).succeeded,
                "headless-undo-without-runtime");
        history.Applied(false);
        Require(document.Layout().Serialize() == initial && !history.CanUndo() && !history.Dirty(initial),
                "headless-drag-is-one-undo-and-restores-clean-state");
        Require(history.CanRedo() &&
                    document.Edit(ReplaceDocumentEdit{SceneLayout::Parse(history.Target(true).json)}, error).succeeded,
                "headless-redo-without-runtime");
        history.Applied(true);
        Require(document.Layout().Serialize() == final && history.Dirty(final), "headless-redo-restores-candidate");
        DirectX::XMFLOAT4X4 identity;
        DirectX::XMStoreFloat4x4(&identity, DirectX::XMMatrixIdentity());
        editor.Select(child.id);
        Require(editor.RotateSelectionWorld(document, {}, identity) &&
                    editor.ScaleSelectionWorld(document, {}, identity, {1, 1, 1}),
                "headless-editor-rotation-and-scale");
        const auto beforeFailure = document.Layout().Serialize();
        Require(!editor.ScaleSelectionWorld(document, {}, identity, {0, 1, 1}) &&
                    !editor.SetLocalTransform(document, child.id, {std::numeric_limits<float>::infinity(), 0, 0}, {},
                                              {1, 1, 1}) &&
                    document.Layout().Serialize() == beforeFailure,
                "headless-invalid-transforms-retain-state");
    }
}

int main()
{
    try
    {
        Validate();
        std::cout << "Document editing contracts: " << contracts << ", failures: 0\n";
        return 0;
    }
    catch (const std::exception& failure)
    {
        std::cerr << "FAIL: " << failure.what() << '\n';
        return 1;
    }
}
