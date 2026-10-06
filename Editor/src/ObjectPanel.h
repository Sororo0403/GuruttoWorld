#pragma once
#include "EditState.h"
#include "HierarchyRows.h"
#include <imgui.h>
#include <string>
#include <optional>

namespace Editor
{
    class ObjectPanel final
    {
    public:
        void Draw(SceneRuntime::SceneWorld& world, EditState& state, bool enabled);
    private:
        void DrawObjects(SceneRuntime::SceneWorld& world, EditState& state, bool enabled);
        std::vector<HierarchyRow> VisibleRows(const SceneRuntime::SceneLayout& layout) const;
        void DrawHierarchy(SceneRuntime::SceneWorld& world, EditState& state, bool enabled);
        void DrawRow(SceneRuntime::SceneWorld& world, EditState& state, const HierarchyRow& row, const std::vector<std::string>& visible);
        void DrawReparentTarget(SceneRuntime::SceneWorld& world, EditState& state, const std::string& parent);
        void DrawParent(SceneRuntime::SceneWorld& world, EditState& state, const SceneRuntime::ScenePlacement& placement);
        void ChangeParent(SceneRuntime::SceneWorld& world, EditState& state, const std::string& id, const std::string& parent);
        std::unordered_set<std::string> collapsed_;
        std::string parentError_;
        bool editsEnabled_=false;
        void DrawInspector(SceneRuntime::SceneWorld& world, EditState& state, bool enabled);
        static void DrawMultiInspector(SceneRuntime::SceneWorld& world, EditState& state, bool enabled);
        void DrawName(SceneRuntime::SceneWorld& world, EditState& state, const SceneRuntime::ScenePlacement& placement);
        static void DrawTransform(SceneRuntime::SceneWorld& world, EditState& state, const SceneRuntime::ScenePlacement& placement);
        void SelectObject(EditState& state, const std::vector<std::string>& visible, const std::string& id);
        std::string anchorId_;
        std::string inspectedId_, observedName_;
        std::vector<char> nameBuffer_;
        bool invalidName_ = false;
        ImGuiTextFilter filter_;
    };
}
