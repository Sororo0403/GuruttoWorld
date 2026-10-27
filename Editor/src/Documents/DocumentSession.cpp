#include "DocumentSession.h"
#include <SceneRuntime/Scene/SceneDocumentProjection.h>

namespace Editor
{
    struct DocumentSession::Request
    {
        std::string interaction;
        bool reset = false, clearSelection = false, reload = false, recordFiles = false;
        std::optional<SceneDocument::OpenCandidate> open;
        std::optional<EditHistory::State> restored;
        std::unique_ptr<EditHistory> history;
        std::vector<FileChange> files;
    };
    struct DocumentSession::Prepared::Impl
    {
        DocumentSession& owner;
        std::unique_ptr<SceneRuntime::SceneDocumentData::Transaction> document;
        std::unique_ptr<SceneRuntime::SceneDocumentProjection::Prepared> projection;
        std::unique_ptr<EditHistory> history;
        SelectionSet selection;
        std::optional<SceneDocument::OpenCandidate> open;
        SceneRuntime::SceneEditResult result;
        bool dirty = false, committed = false, active = true;
        explicit Impl(DocumentSession& session) : owner(session)
        {
            owner.preparing_ = true;
        }
        ~Impl()
        {
            if (active)
                owner.preparing_ = false;
        }
    };
    DocumentSession::Prepared::Prepared(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
    DocumentSession::Prepared::~Prepared() = default;
    bool DocumentSession::Prepared::Commit() noexcept
    {
        auto& value = *impl_;
        if (value.committed || !value.document->Commit())
            return false;
        if (value.projection)
            value.projection->Commit();
        value.owner.history_.Swap(*value.history);
        value.owner.editing_.CommitSelection(value.selection);
        value.owner.editing_.SetChanged(value.dirty);
        if (value.open)
            value.owner.document_.AcceptOpen(*value.open);
        value.committed = true;
        if (value.projection)
            value.projection->Finish();
        value.document->Finish();
        value.owner.preparing_ = false;
        value.active = false;
        return true;
    }
    const SceneRuntime::SceneEditResult& DocumentSession::Prepared::Result() const noexcept
    {
        return impl_->result;
    }
    const std::vector<std::string>& DocumentSession::Prepared::CreatedIds() const noexcept
    {
        return impl_->document->Plan().createdIds;
    }
    DocumentSession::DocumentSession(std::filesystem::path path, std::shared_ptr<const Engine::ContentContext> context)
        : document_(std::move(path), std::move(context))
    {
        document_.Data()->AttachEditor(*this);
    }
    DocumentSession::~DocumentSession()
    {
        document_.Data()->DetachEditor(*this);
    }
    SceneDocument& DocumentSession::Document() noexcept
    {
        return document_;
    }
    EditState& DocumentSession::Editing() noexcept
    {
        return editing_;
    }
    const EditHistory& DocumentSession::History() const noexcept
    {
        return history_;
    }
    const SceneRuntime::SceneLayout& DocumentSession::Layout() const
    {
        return document_.Layout();
    }
    const std::string& DocumentSession::Json()
    {
        return snapshots_.Get(Layout());
    }
    EditHistory::State DocumentSession::Snapshot(const std::string& json) const
    {
        return {json, editing_.SelectedId(), editing_.SelectedIds()};
    }
    std::unique_ptr<DocumentSession::Prepared> DocumentSession::PrepareRequest(const SceneRuntime::SceneEdit& edit,
                                                                               Request request, std::string& error)
    {
        if (preparing_)
        {
            error = "Document session is already preparing an edit";
            return {};
        }
        try
        {
            auto value = std::make_unique<Prepared::Impl>(*this);
            value->document = document_.Data()->BeginEdit(edit, error, this);
            if (!value->document)
                return {};
            auto& plan = value->document->Plan();
            if (auto* projection = document_.Data()->Projection())
            {
                value->projection = projection->PrepareDocument(plan, request.reload, error);
                if (!value->projection)
                    return {};
            }
            if (!value->document->PrepareCommit(error))
                return {};
            value->result = {true, plan.changes};
            value->selection.Restore(editing_.SelectedIds(), editing_.SelectedId());
            if (request.restored)
                value->selection.Restore(request.restored->selections, request.restored->selection);
            else if (request.clearSelection)
                value->selection.Restore({}, {});
            else if (!plan.createdIds.empty())
                value->selection.Restore(plan.createdIds, plan.createdIds.back());
            else
            {
                auto ids = editing_.SelectedIds();
                std::erase_if(ids,
                              [&](const auto& id)
                              {
                                  return std::none_of(plan.layout.objects.begin(), plan.layout.objects.end(),
                                                      [&](const auto& object)
                                                      {
                                                          return object.id == id;
                                                      });
                              });
                auto primary = editing_.SelectedId();
                if (std::find(ids.begin(), ids.end(), primary) == ids.end())
                    primary.clear();
                value->selection.Restore(ids, std::move(primary));
            }
            value->history = request.history ? std::move(request.history) : std::make_unique<EditHistory>(history_);
            const auto before = Snapshot(Json());
            EditHistory::State after{plan.layout.Serialize(), value->selection.Primary(), value->selection.Ids()};
            if (request.reset)
                value->history->Reset(after);
            else if (!request.restored)
            {
                if (value->history->Empty())
                    value->history->Reset(before);
                if (request.recordFiles)
                    value->history->RecordTransaction(before, after, std::move(request.files));
                else
                    value->history->Observe(after, request.interaction);
            }
            const bool unsaved = request.open ? request.open->create_ : document_.UnsavedNew();
            value->dirty = unsaved || value->history->Dirty(after.json);
            value->open = std::move(request.open);
            error.clear();
            return std::unique_ptr<Prepared>(new Prepared(std::move(value)));
        }
        catch (const std::exception& failure)
        {
            error = failure.what();
            return {};
        }
    }
    std::unique_ptr<DocumentSession::Prepared> DocumentSession::Prepare(const SceneRuntime::SceneEdit& edit,
                                                                        std::string& error)
    {
        Request request;
        request.interaction = editing_.Interaction();
        return PrepareRequest(edit, std::move(request), error);
    }
    SceneRuntime::SceneEditResult DocumentSession::Edit(const SceneRuntime::SceneEdit& edit, std::string& error)
    {
        auto prepared = Prepare(edit, error);
        if (!prepared)
            return {};
        auto result = prepared->Result();
        if (!prepared->Commit())
            return {};
        return result;
    }
    bool DocumentSession::Open(const std::filesystem::path& path, std::string& error)
    {
        try
        {
            Request request;
            request.open = SceneDocument::PrepareOpen(path);
            auto layout = request.open->layout_;
            request.reset = request.clearSelection = request.reload = true;
            auto prepared =
                PrepareRequest(SceneRuntime::ReplaceDocumentEdit{std::move(layout)}, std::move(request), error);
            return prepared && prepared->Commit();
        }
        catch (const std::exception& failure)
        {
            error = failure.what();
            return false;
        }
    }
    bool DocumentSession::ApplyPending(std::string& error)
    {
        if (!document_.Ready())
            return false;
        try
        {
            Request request;
            const auto pending = *document_.request_;
            document_.Cancel();
            request.open = SceneDocument::PrepareOpen(pending.path, pending.create);
            auto layout = request.open->layout_;
            request.reset = request.clearSelection = true;
            request.reload = !pending.create;
            auto prepared =
                PrepareRequest(SceneRuntime::ReplaceDocumentEdit{std::move(layout)}, std::move(request), error);
            return prepared && prepared->Commit();
        }
        catch (const std::exception& failure)
        {
            document_.Cancel();
            error = failure.what();
            return false;
        }
    }
    bool DocumentSession::ApplyFiles(SceneRuntime::SceneLayout layout, std::vector<FileChange> files,
                                     std::string& error)
    {
        return FileTransaction::Apply(files, true, error,
                                      [&]()
                                      {
                                          Request request;
                                          request.recordFiles = true;
                                          request.files = files;
                                          auto prepared =
                                              PrepareRequest(SceneRuntime::ReplaceDocumentEdit{std::move(layout)},
                                                             std::move(request), error);
                                          return prepared && prepared->Commit();
                                      });
    }
    bool DocumentSession::RestoreHistory(bool redo, std::string& error)
    {
        if (preparing_)
            return false;
        try
        {
            auto history = std::make_unique<EditHistory>(history_);
            history->Commit();
            if (!(redo ? history->CanRedo() : history->CanUndo()))
            {
                error = "History has no target";
                return false;
            }
            auto target = history->Target(redo);
            const auto files = history->TargetFiles(redo);
            history->Applied(redo);
            return FileTransaction::Apply(
                files, redo, error,
                [&]()
                {
                    Request request;
                    request.restored = target;
                    request.history = std::move(history);
                    auto prepared =
                        PrepareRequest(SceneRuntime::ReplaceDocumentEdit{SceneRuntime::SceneLayout::Parse(target.json)},
                                       std::move(request), error);
                    return prepared && prepared->Commit();
                });
        }
        catch (const std::exception& failure)
        {
            error = failure.what();
            return false;
        }
    }
    void DocumentSession::ObserveCurrent()
    {
        if (preparing_)
            return;
        const auto& json = Json();
        if (history_.Matches(json, editing_.SelectedId(), editing_.SelectedIds(), editing_.Interaction()))
            return;
        auto candidate = history_;
        candidate.Observe(json, editing_.SelectedId(), editing_.SelectedIds(), editing_.Interaction());
        history_.Swap(candidate);
        editing_.SetChanged(document_.UnsavedNew() || history_.Dirty(json));
    }
    void DocumentSession::CommitInteraction()
    {
        if (preparing_)
            return;
        auto candidate = history_;
        candidate.Commit();
        history_.Swap(candidate);
    }
    void DocumentSession::ResetHistory()
    {
        if (preparing_)
            return;
        EditHistory candidate;
        candidate.Reset(Snapshot(Json()));
        history_.Swap(candidate);
    }
    void DocumentSession::MarkSaved()
    {
        if (preparing_)
            return;
        auto candidate = history_;
        candidate.Saved(Json());
        history_.Swap(candidate);
        editing_.SetChanged(document_.UnsavedNew());
    }
}
