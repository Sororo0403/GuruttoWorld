#pragma once
#include <SceneRuntime/SceneWorld.h>
#include <imgui.h>
#include <string>

namespace Editor
{
    class ObjectPanel final
    {
    public:
        void Draw(SceneRuntime::SceneWorld& world);
        bool HasChanges() const { return changed_; }
    private:
        ImGuiTextFilter filter_;
        std::string selectedId_;
        bool changed_ = false;
        bool invalidTransform_ = false;
    };
}
