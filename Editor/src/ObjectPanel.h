#pragma once
#include <SceneRuntime/SceneWorld.h>
#include <imgui.h>
#include <string>
#include <optional>

namespace Editor
{
    enum class ObjectAction { Add, Duplicate, Delete };
    struct ObjectRequest
    {
        ObjectAction action;
        std::string id;
        std::filesystem::path model;
        std::array<float, 3> position{};
    };

    class ObjectPanel final
    {
    public:
        void ScanModels(const std::filesystem::path& root);
        void Draw(SceneRuntime::SceneWorld& world, const std::array<float, 3>& suggestedPosition, bool enabled);
        std::optional<ObjectRequest> TakeRequest();
        void ObjectChanged(std::string id) { selectedId_ = std::move(id); changed_ = true; invalidTransform_ = false; }
        const std::string& SelectedId() const { return selectedId_; }
        void Select(std::string id) { selectedId_ = std::move(id); invalidTransform_ = false; }
        bool HasChanges() const { return changed_; }
        void MarkSaved() { changed_ = false; }
        void Reloaded() { changed_ = false; invalidTransform_ = false; }
    private:
        void DrawModels(const std::array<float, 3>& suggestedPosition, bool enabled);
        std::vector<std::filesystem::path> models_;
        std::filesystem::path selectedModel_;
        ImGuiTextFilter modelFilter_;
        std::string catalogError_;
        std::array<float, 3> addPosition_{};
        bool positionInitialized_ = false;
        std::optional<ObjectRequest> request_;
        ImGuiTextFilter filter_;
        std::string selectedId_;
        bool changed_ = false;
        bool invalidTransform_ = false;
    };
}
