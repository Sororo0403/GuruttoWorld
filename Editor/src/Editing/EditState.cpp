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

    auto EditState::SetLocalTransform(SceneRuntime::SceneEditTarget& world, const std::string& id,
                                      const std::array<float, 3>& position, const std::array<float, 3>& rotation,
                                      const std::array<float, 3>& scale) -> bool
    {
        std::string error;
        const auto result = world.Edit(SceneRuntime::TransformEdit{id, {position, rotation, scale}}, error);
        invalidTransform_ = !result.succeeded;
        if (result.succeeded && !result.changes.Empty())
            changed_ = true;
        return result.succeeded;
    }

    auto EditState::TranslateSelectionWorld(SceneRuntime::SceneEditTarget& world, const std::array<float, 3>& delta)
        -> bool
    {
        std::string error;
        const auto result = world.Edit(SceneRuntime::TranslationEdit{SelectedIds(), delta}, error);
        invalidTransform_ = !result.succeeded;
        if (result.succeeded && !result.changes.Empty())
            changed_ = true;
        return result.succeeded;
    }

    auto EditState::RotateSelectionWorld(SceneRuntime::SceneEditTarget& world, const std::array<float, 3>& pivot,
                                         const DirectX::XMFLOAT4X4& rotation) -> bool
    {
        std::string error;
        const auto result = world.Edit(SceneRuntime::RotationEdit{SelectedIds(), pivot, rotation}, error);
        invalidTransform_ = !result.succeeded;
        if (result.succeeded && !result.changes.Empty())
            changed_ = true;
        return result.succeeded;
    }

    auto EditState::ScaleSelectionWorld(SceneRuntime::SceneEditTarget& world, const std::array<float, 3>& pivot,
                                        const DirectX::XMFLOAT4X4& axes, const std::array<float, 3>& factors) -> bool
    {
        std::string error;
        const auto result = world.Edit(SceneRuntime::ScaleEdit{SelectedIds(), pivot, axes, factors}, error);
        invalidTransform_ = !result.succeeded;
        if (result.succeeded && !result.changes.Empty())
            changed_ = true;
        return result.succeeded;
    }

    auto EditState::SetParent(SceneRuntime::SceneEditTarget& world, const std::string& id, std::string parentId,
                              std::string& error) -> bool
    {
        const auto result = world.Edit(SceneRuntime::ParentEdit{{id}, parentId}, error);
        if (!result.succeeded)
            return false;
        if (!result.changes.Empty())
        {
            changed_ = true;
            SetInteraction("command/reparent/" + id);
        }
        return true;
    }

    auto EditState::SetParents(SceneRuntime::SceneEditTarget& world, const std::vector<std::string>& ids,
                               const std::string& parent, std::string& error) -> bool
    {
        const auto result = world.Edit(SceneRuntime::ParentEdit{ids, parent}, error);
        if (!result.succeeded)
            return false;
        if (!result.changes.Empty())
        {
            changed_ = true;
            SetInteraction("command/reparent/selection");
        }
        return true;
    }

    auto EditState::Rename(SceneRuntime::SceneEditTarget& world, const std::string& id, std::string name) -> bool
    {
        std::string error;
        const auto result = world.Edit(SceneRuntime::NameEdit{id, std::move(name)}, error);
        if (result.succeeded && !result.changes.Empty())
            changed_ = true;
        return result.succeeded;
    }

    auto EditState::DuplicateSelectionRequest() const -> ObjectRequest
    {
        ObjectRequest request;
        request.action = ObjectAction::Duplicate;
        request.ids = SelectedIds();
        return request;
    }

    auto EditState::DuplicateObjects(SceneRuntime::SceneEditTarget& world, const std::vector<std::string>& ids,
                                     const std::array<float, 3>& offset, std::string& error) -> bool
    {
        const auto result = world.Edit(SceneRuntime::DuplicateEdit{ids, offset}, error);
        if (!result.succeeded)
            return false;
        std::vector<std::string> created;
        for (const auto& change : result.changes.objects)
            if (change.kind == SceneRuntime::SceneChangeKind::Added)
                created.push_back(change.objectId);
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

    auto EditState::DeleteObjects(SceneRuntime::SceneEditTarget& world, const std::vector<std::string>& ids,
                                  std::string& error) -> bool
    {
        if (!world.Edit(SceneRuntime::RemoveObjectsEdit{ids}, error).succeeded)
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

    auto EditState::ResetTransform(SceneRuntime::SceneEditTarget& world, const std::string& id) -> bool
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

}
