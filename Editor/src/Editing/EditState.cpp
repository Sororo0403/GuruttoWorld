#include "EditState.h"

namespace Editor
{
    auto EditState::ObjectChanged(std::string id) -> void
    {
        inspectedAsset_.clear();
        selection_.Select(std::move(id));
        changed_ = true;
        invalidTransform_ = false;
    }

    auto EditState::RestoreSelection(const std::vector<std::string>& ids, const std::string& primary) -> void
    {
        inspectedAsset_.clear();
        selection_.Restore(ids, primary);
        invalidTransform_ = false;
    }

    auto EditState::SelectRange(const std::vector<std::string>& visible, const std::string& anchor,
                                const std::string& clicked, bool additive) -> void
    {
        inspectedAsset_.clear();
        selection_.Range(visible, anchor, clicked, additive);
        invalidTransform_ = false;
    }

    auto EditState::Select(std::string id, bool additive) -> void
    {
        inspectedAsset_.clear();
        selection_.Select(std::move(id), additive);
        invalidTransform_ = false;
    }

    auto EditState::Reloaded() -> void
    {
        inspectedAsset_.clear();
        selection_.Select({});
        changed_ = false;
        invalidTransform_ = false;
        interaction_.clear();
    }

    auto EditState::SetLocalTransform(SceneRuntime::SceneWorld& world, const std::string& id,
                                      const std::array<float, 3>& position, const std::array<float, 3>& rotation,
                                      const std::array<float, 3>& scale) -> bool
    {
        const auto& objects = world.Layout().objects;
        const auto found = std::find_if(objects.begin(), objects.end(),
                                        [&](const auto& object)
                                        {
                                            return object.id == id;
                                        });
        const bool changed = found != objects.end() &&
                             (found->position != position || found->rotation != rotation || found->scale != scale);
        invalidTransform_ = !world.SetLocalTransform(id, position, rotation, scale);
        if (!invalidTransform_ && changed)
            changed_ = true;
        return !invalidTransform_;
    }

    auto EditState::TranslateSelectionWorld(SceneRuntime::SceneWorld& world, const std::array<float, 3>& delta) -> bool
    {
        const auto& before = world.Layout().objects;
        std::vector<std::array<float, 3>> previous(before.size());
        std::transform(before.begin(), before.end(), previous.begin(),
                       [](const auto& object)
                       {
                           return object.position;
                       });
        invalidTransform_ = !world.TranslateObjectsWorld(SelectedIds(), delta);
        const auto& after = world.Layout().objects;
        if (!invalidTransform_ && !std::equal(previous.begin(), previous.end(), after.begin(),
                                              [](const auto& position, const auto& object)
                                              {
                                                  return position == object.position;
                                              }))
            changed_ = true;
        return !invalidTransform_;
    }

    auto EditState::RotateSelectionWorld(SceneRuntime::SceneWorld& world, const std::array<float, 3>& pivot,
                                         const DirectX::XMFLOAT4X4& rotation) -> bool
    {
        const auto before = world.Layout().objects;
        invalidTransform_ = !world.RotateObjectsWorld(SelectedIds(), pivot, rotation);
        TrackTransformChange(world, before);
        return !invalidTransform_;
    }

    auto EditState::ScaleSelectionWorld(SceneRuntime::SceneWorld& world, const std::array<float, 3>& pivot,
                                        const DirectX::XMFLOAT4X4& axes, const std::array<float, 3>& factors) -> bool
    {
        const auto before = world.Layout().objects;
        invalidTransform_ = !world.ScaleObjectsWorld(SelectedIds(), pivot, axes, factors);
        TrackTransformChange(world, before);
        return !invalidTransform_;
    }

    auto EditState::SetParent(SceneRuntime::SceneWorld& world, const std::string& id, std::string parentId,
                              std::string& error) -> bool
    {
        const auto& objects = world.Layout().objects;
        const auto found = std::find_if(objects.begin(), objects.end(),
                                        [&](const auto& object)
                                        {
                                            return object.id == id;
                                        });
        const bool changed = found != objects.end() && found->parentId != parentId;
        if (!world.SetParent(id, std::move(parentId), error))
            return false;
        if (changed)
        {
            changed_ = true;
            SetInteraction("command/reparent/" + id);
        }
        return true;
    }

    auto EditState::SetParents(SceneRuntime::SceneWorld& world, const std::vector<std::string>& ids,
                               const std::string& parent, std::string& error) -> bool
    {
        const auto changed = std::any_of(world.Layout().objects.begin(), world.Layout().objects.end(),
                                         [&](const auto& object)
                                         {
                                             return std::find(ids.begin(), ids.end(), object.id) != ids.end() &&
                                                    object.parentId != parent;
                                         });
        if (!world.SetParents(ids, parent, error))
            return false;
        if (changed)
        {
            changed_ = true;
            SetInteraction("command/reparent/selection");
        }
        return true;
    }

    auto EditState::Rename(SceneRuntime::SceneWorld& world, const std::string& id, std::string name) -> bool
    {
        const auto& objects = world.Layout().objects;
        const auto found = std::find_if(objects.begin(), objects.end(),
                                        [&](const auto& object)
                                        {
                                            return object.id == id;
                                        });
        const bool changed = found != objects.end() && found->name != name;
        if (!world.RenameObject(id, std::move(name)))
            return false;
        if (changed)
            changed_ = true;
        return true;
    }

    auto EditState::DuplicateSelectionRequest() const -> ObjectRequest
    {
        ObjectRequest request;
        request.action = ObjectAction::Duplicate;
        request.ids = SelectedIds();
        return request;
    }

    auto EditState::DuplicateObjects(SceneRuntime::SceneWorld& world, const std::vector<std::string>& ids,
                                     const std::array<float, 3>& offset, std::string& error) -> bool
    {
        std::vector<std::string> created;
        if (!world.DuplicateObjects(ids, offset, created, error))
            return false;
        selection_.Restore(created, created.back());
        changed_ = true;
        invalidTransform_ = false;
        return true;
    }

    auto EditState::DeleteSelectionRequest() const -> ObjectRequest
    {
        ObjectRequest request;
        request.action = ObjectAction::Delete;
        request.ids = SelectedIds();
        return request;
    }

    auto EditState::DeleteObjects(SceneRuntime::SceneWorld& world, const std::vector<std::string>& ids,
                                  std::string& error) -> bool
    {
        if (!world.RemoveObjects(ids, error))
            return false;
        auto remaining = SelectedIds();
        auto primary = SelectedId();
        std::erase_if(remaining,
                      [&](const auto& id)
                      {
                          return std::find(ids.begin(), ids.end(), id) != ids.end();
                      });
        if (std::find(remaining.begin(), remaining.end(), primary) == remaining.end())
            primary.clear();
        selection_.Restore(remaining, primary);
        changed_ = true;
        invalidTransform_ = false;
        return true;
    }

    auto EditState::ResetTransform(SceneRuntime::SceneWorld& world, const std::string& id) -> bool
    {
        if (!SetLocalTransform(world, id, {0, 0, 0}, {0, 0, 0}, {1, 1, 1}))
            return false;
        SetInteraction("command/reset/" + id);
        return true;
    }

    auto EditState::RequestComponents(SceneRuntime::ScenePlacement placement, std::string interaction) -> void
    {
        ObjectRequest request;
        request.action = ObjectAction::Components;
        request.id = placement.id;
        request.components = std::move(placement);
        request.interaction = std::move(interaction);
        Request(std::move(request));
    }

    auto EditState::RequestComponentBatch(std::vector<SceneRuntime::ScenePlacement> placements, std::string interaction)
        -> void
    {
        if (placements.empty())
            return;
        ObjectRequest request;
        request.action = ObjectAction::Components;
        request.componentBatch = std::move(placements);
        request.interaction = std::move(interaction);
        Request(std::move(request));
    }

    auto EditState::TakeRequest() -> std::optional<ObjectRequest>
    {
        auto request = std::move(request_);
        request_.reset();
        return request;
    }

    auto EditState::TrackTransformChange(const SceneRuntime::SceneWorld& world,
                                         const std::vector<SceneRuntime::ScenePlacement>& before) -> void
    {
        const auto& after = world.Layout().objects;
        if (!invalidTransform_ && !std::equal(before.begin(), before.end(), after.begin(),
                                              [](const auto& previous, const auto& current)
                                              {
                                                  return previous.position == current.position &&
                                                         previous.rotation == current.rotation &&
                                                         previous.scale == current.scale;
                                              }))
            changed_ = true;
    }
}
