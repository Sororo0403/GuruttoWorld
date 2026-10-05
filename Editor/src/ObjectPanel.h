#pragma once
#include "EditState.h"
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
        void DrawObjects(const SceneRuntime::SceneWorld& world, EditState& state, bool enabled);
        void DrawInspector(SceneRuntime::SceneWorld& world, EditState& state, bool enabled);
        void DrawName(SceneRuntime::SceneWorld& world, EditState& state, const SceneRuntime::ScenePlacement& placement);
        static void DrawTransform(SceneRuntime::SceneWorld& world, EditState& state, const SceneRuntime::ScenePlacement& placement);
        std::string inspectedId_, observedName_;
        std::vector<char> nameBuffer_;
        bool invalidName_ = false;
        ImGuiTextFilter filter_;
    };
}
