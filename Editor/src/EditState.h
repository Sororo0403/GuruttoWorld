#pragma once
#include <SceneRuntime/SceneWorld.h>
#include <optional>
#include <algorithm>
#include "SelectionSet.h"
#include <string>
#include <utility>

namespace Editor
{
    enum class ObjectAction { Add, AddEmpty, Duplicate, Delete };
    struct ObjectRequest
    {
        ObjectAction action = ObjectAction::Add;
        std::string id;
        std::filesystem::path model;
        std::array<float, 3> position{};
        std::vector<std::string> ids{};
    };

    // Shared editing state and operations; independent of panels and ImGui.
    class EditState final
    {
    public:
        void BeginFrame() { interaction_.clear(); }
        void SetInteraction(std::string key) { interaction_=std::move(key); }
        const std::string& Interaction() const { return interaction_; }
        void ObjectChanged(std::string id) { inspectedAsset_.clear(); selection_.Select(std::move(id)); changed_ = true; invalidTransform_ = false; }
        void InspectAsset(std::filesystem::path path) { inspectedAsset_=std::move(path); }
        const std::filesystem::path& InspectedAsset() const { return inspectedAsset_; }
        const std::string& SelectedId() const { return selection_.Primary(); }
        const std::vector<std::string>& SelectedIds() const { return selection_.Ids(); }
        bool IsSelected(const std::string& id) const { return selection_.Contains(id); }
        bool SingleSelection() const { return SelectedIds().size()==1; }
        void RestoreSelection(const std::vector<std::string>& ids, const std::string& primary)
        { inspectedAsset_.clear(); selection_.Restore(ids,primary); invalidTransform_=false; }
        void SelectRange(const std::vector<std::string>& visible, const std::string& anchor,
            const std::string& clicked, bool additive)
        { inspectedAsset_.clear(); selection_.Range(visible,anchor,clicked,additive); invalidTransform_=false; }
        void Select(std::string id, bool additive=false) { inspectedAsset_.clear(); selection_.Select(std::move(id),additive); invalidTransform_ = false; }
        bool HasChanges() const { return changed_; }
        void SetChanged(bool changed) { changed_ = changed; }
        void MarkSaved() { changed_ = false; }
        void Reloaded() { inspectedAsset_.clear(); selection_.Select({}); changed_ = false; invalidTransform_ = false; interaction_.clear(); }
        bool SetLocalTransform(SceneRuntime::SceneWorld& world, const std::string& id,
            const std::array<float, 3>& position, const std::array<float, 3>& rotation,
            const std::array<float, 3>& scale)
        {
            const auto& objects=world.Layout().objects;
            const auto found=std::find_if(objects.begin(),objects.end(),[&](const auto& object) { return object.id==id; });
            const bool changed=found!=objects.end() && (found->position!=position || found->rotation!=rotation || found->scale!=scale);
            invalidTransform_=!world.SetLocalTransform(id,position,rotation,scale);
            if (!invalidTransform_ && changed) changed_=true;
            return !invalidTransform_;
        }
        bool TranslateSelectionWorld(SceneRuntime::SceneWorld& world, const std::array<float,3>& delta)
        {
            const auto& before=world.Layout().objects;
            std::vector<std::array<float,3>> previous(before.size());
            std::transform(before.begin(),before.end(),previous.begin(),[](const auto& object) { return object.position; });
            invalidTransform_=!world.TranslateObjectsWorld(SelectedIds(),delta);
            const auto& after=world.Layout().objects;
            if (!invalidTransform_ && !std::equal(previous.begin(),previous.end(),after.begin(),
                [](const auto& position,const auto& object) { return position==object.position; })) changed_=true;
            return !invalidTransform_;
        }
        bool RotateSelectionWorld(SceneRuntime::SceneWorld& world, const std::array<float,3>& pivot,
            const DirectX::XMFLOAT4X4& rotation)
        {
            const auto before=world.Layout().objects;
            invalidTransform_=!world.RotateObjectsWorld(SelectedIds(),pivot,rotation);
            TrackTransformChange(world,before);
            return !invalidTransform_;
        }
        bool ScaleSelectionWorld(SceneRuntime::SceneWorld& world, const std::array<float,3>& pivot,
            const DirectX::XMFLOAT4X4& axes, const std::array<float,3>& factors)
        {
            const auto before=world.Layout().objects;
            invalidTransform_=!world.ScaleObjectsWorld(SelectedIds(),pivot,axes,factors);
            TrackTransformChange(world,before);
            return !invalidTransform_;
        }
        bool SetParent(SceneRuntime::SceneWorld& world, const std::string& id, std::string parentId, std::string& error)
        {
            const auto& objects=world.Layout().objects;
            const auto found=std::find_if(objects.begin(),objects.end(),[&](const auto& object) { return object.id==id; });
            const bool changed=found!=objects.end() && found->parentId!=parentId;
            if (!world.SetParent(id,std::move(parentId),error)) return false;
            if (changed) { changed_=true; SetInteraction("command/reparent/"+id); }
            return true;
        }
        bool Rename(SceneRuntime::SceneWorld& world, const std::string& id, std::string name)
        {
            const auto& objects=world.Layout().objects;
            const auto found=std::find_if(objects.begin(),objects.end(),[&](const auto& object) { return object.id==id; });
            const bool changed=found!=objects.end() && found->name!=name;
            if (!world.RenameObject(id,std::move(name))) return false;
            if (changed) changed_=true;
            return true;
        }
        ObjectRequest DuplicateSelectionRequest() const
        {
            ObjectRequest request;
            request.action=ObjectAction::Duplicate;
            request.ids=SelectedIds();
            return request;
        }
        bool DuplicateObjects(SceneRuntime::SceneWorld& world, const std::vector<std::string>& ids,
            const std::array<float,3>& offset, std::string& error)
        {
            std::vector<std::string> created;
            if (!world.DuplicateObjects(ids,offset,created,error)) return false;
            selection_.Restore(created,created.back());
            changed_=true; invalidTransform_=false;
            return true;
        }
        ObjectRequest DeleteSelectionRequest() const
        {
            ObjectRequest request;
            request.action=ObjectAction::Delete;
            request.ids=SelectedIds();
            return request;
        }
        bool DeleteObjects(SceneRuntime::SceneWorld& world, const std::vector<std::string>& ids, std::string& error)
        {
            if (!world.RemoveObjects(ids,error)) return false;
            auto remaining=SelectedIds();
            auto primary=SelectedId();
            std::erase_if(remaining,[&](const auto& id) { return std::find(ids.begin(),ids.end(),id)!=ids.end(); });
            if (std::find(remaining.begin(),remaining.end(),primary)==remaining.end()) primary.clear();
            selection_.Restore(remaining,primary);
            changed_=true; invalidTransform_=false;
            return true;
        }
        bool ResetTransform(SceneRuntime::SceneWorld& world, const std::string& id)
        {
            if (!SetLocalTransform(world,id,{0,0,0},{0,0,0},{1,1,1})) return false;
            SetInteraction("command/reset/"+id);
            return true;
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
        void TrackTransformChange(const SceneRuntime::SceneWorld& world, const std::vector<SceneRuntime::ScenePlacement>& before)
        {
            const auto& after=world.Layout().objects;
            if (!invalidTransform_ && !std::equal(before.begin(),before.end(),after.begin(),
                [](const auto& previous,const auto& current) {
                    return previous.position==current.position && previous.rotation==current.rotation && previous.scale==current.scale;
                })) changed_=true;
        }
        std::filesystem::path inspectedAsset_;
        SelectionSet selection_;
        std::string interaction_;
        bool changed_ = false;
        bool invalidTransform_ = false;
        std::optional<ObjectRequest> request_;
    };
}
