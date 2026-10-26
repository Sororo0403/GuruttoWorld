#include <SceneRuntime/Scene/SceneDocumentData.h>

namespace SceneRuntime
{
    SceneEditResult SceneDocumentData::Edit(const SceneEdit& edit, std::string& error)
    {
        if (view_)
        {
            error = "Projected document edits require a projection transaction";
            return {};
        }
        auto plan = SceneEdits::Prepare(layout_, edit, nextId_, error);
        if (!plan)
            return {};
        SceneEditResult result{true, plan->changes};
        if (result.changes.Empty())
            return result;
        auto changes = changes_;
        changes.Merge(plan->changes);
        layout_ = std::move(plan->layout);
        nextId_ = plan->nextId;
        changes_.Swap(changes);
        return result;
    }

    SceneChangeSet SceneDocumentData::TakeChanges() noexcept
    {
        SceneChangeSet changes;
        changes_.Swap(changes);
        return changes;
    }

    auto SceneDocumentData::operator=(const SceneDocumentData& other) -> SceneDocumentData&
    {
        if (this == &other)
            return *this;
        if (view_ && (layout_.Serialize() != other.layout_.Serialize() || context_ != other.context_))
            throw std::runtime_error("Document changes require a projection transaction");
        layout_ = other.layout_;
        context_ = other.context_;
        return *this;
    }
}
