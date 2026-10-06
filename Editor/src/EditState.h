#pragma once
#include <SceneRuntime/SceneWorld.h>
#include <optional>
#include "SelectionSet.h"
#include <string>
#include <utility>

namespace Editor
{
    enum class ObjectAction { Add, Duplicate, Delete };
    struct ObjectRequest
    {
        ObjectAction action = ObjectAction::Add;
        std::string id;
        std::filesystem::path model;
        std::array<float, 3> position{};
    };

    // Shared editing state and operations; independent of panels and ImGui.
    class EditState final
    {
    public:
        void ObjectChanged(std::string id) { selection_.Select(std::move(id)); changed_ = true; invalidTransform_ = false; }
        const std::string& SelectedId() const { return selection_.Primary(); }
        const std::vector<std::string>& SelectedIds() const { return selection_.Ids(); }
        bool IsSelected(const std::string& id) const { return selection_.Contains(id); }
        bool SingleSelection() const { return SelectedIds().size()==1; }
        void RestoreSelection(const std::vector<std::string>& ids, const std::string& primary)
        { selection_.Restore(ids,primary); invalidTransform_=false; }
        void SelectRange(const std::vector<std::string>& visible, const std::string& anchor,
            const std::string& clicked, bool additive)
        { selection_.Range(visible,anchor,clicked,additive); invalidTransform_=false; }
        void Select(std::string id, bool additive=false) { selection_.Select(std::move(id),additive); invalidTransform_ = false; }
        bool HasChanges() const { return changed_; }
        void SetChanged(bool changed) { changed_ = changed; }
        void MarkSaved() { changed_ = false; }
        void Reloaded() { selection_.Select({}); changed_ = false; invalidTransform_ = false; }
        bool SetInspectorTransform(SceneRuntime::SceneWorld& world, const std::string& id,
            const std::array<float, 3>& position, const std::array<float, 3>& rotation,
            const std::array<float, 3>& scale)
        {
            if (world.Layout().transformSpace==SceneRuntime::TransformSpace::Local)
                invalidTransform_=!world.SetLocalTransform(id,position,rotation,scale);
            else invalidTransform_=!world.SetWorldTransform(id,position,rotation,scale);
            if (!invalidTransform_) changed_ = true;
            return !invalidTransform_;
        }
        bool TranslateSelectionWorld(SceneRuntime::SceneWorld& world, const std::array<float,3>& delta)
        {
            invalidTransform_=!world.TranslateObjectsWorld(SelectedIds(),delta);
            if (!invalidTransform_ && delta!=std::array<float,3>{}) changed_=true;
            return !invalidTransform_;
        }
        bool EnableParentTransforms(SceneRuntime::SceneWorld& world, std::string& error)
        {
            const bool changed=world.Layout().transformSpace==SceneRuntime::TransformSpace::World;
            if (!world.EnableParentTransforms(error)) return false;
            if (changed) changed_=true;
            invalidTransform_=false;
            return true;
        }
        bool SetParent(SceneRuntime::SceneWorld& world, const std::string& id, std::string parentId, std::string& error)
        {
            const auto& objects=world.Layout().objects;
            const auto found=std::find_if(objects.begin(),objects.end(),[&](const auto& object) { return object.id==id; });
            const bool changed=found!=objects.end() && found->parentId!=parentId;
            if (!world.SetParent(id,std::move(parentId),error)) return false;
            if (changed) changed_=true;
            return true;
        }
        bool Rename(SceneRuntime::SceneWorld& world, const std::string& id, std::string name)
        {
            if (!world.RenameObject(id, std::move(name))) return false;
            changed_ = true;
            return true;
        }
        bool ResetTransform(SceneRuntime::SceneWorld& world, const std::string& id)
        {
            return SetInspectorTransform(world, id, {0,0,0}, {0,0,0}, {1,1,1});
        }
        bool InvalidTransform() const { return invalidTransform_; }
        void Request(ObjectRequest request) { request_ = std::move(request); }
        std::optional<ObjectRequest> TakeRequest()
        {
            auto request = std::move(request_);
            request_.reset();
            return request;
        }
    private:
        SelectionSet selection_;
        bool changed_ = false;
        bool invalidTransform_ = false;
        std::optional<ObjectRequest> request_;
    };
}
