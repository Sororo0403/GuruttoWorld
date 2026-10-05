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
        void ScanModels(const std::filesystem::path& root);
        void Draw(SceneRuntime::SceneWorld& world, EditState& state, const std::array<float, 3>& suggestedPosition, bool enabled);
    private:
        void DrawObjects(const SceneRuntime::SceneWorld& world, EditState& state, bool enabled);
        static void DrawInspector(SceneRuntime::SceneWorld& world, EditState& state, bool enabled);
        void DrawModels(EditState& state, const std::array<float, 3>& suggestedPosition, bool enabled);
        std::vector<std::filesystem::path> models_;
        std::filesystem::path selectedModel_;
        ImGuiTextFilter modelFilter_;
        std::string catalogError_;
        std::array<float, 3> addPosition_{};
        bool positionInitialized_ = false;
        ImGuiTextFilter filter_;
    };
}
