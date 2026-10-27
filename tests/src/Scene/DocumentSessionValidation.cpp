#include "../../../Editor/src/Documents/DocumentSession.h"
#include <iostream>
#include <chrono>
#include <functional>

#if defined(__d3d12_h__) || defined(_WINDOWS_)
#error Document sessions must not include rendering or platform APIs
#endif

namespace
{
    size_t contracts = 0;
    void Require(bool value, const char* name)
    {
        ++contracts;
        if (!value)
            throw std::runtime_error(name);
        std::cout << "PASS: " << name << '\n';
    }
    std::optional<std::string> Read(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            return {};
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }
    void Write(const std::filesystem::path& path, const std::string& value)
    {
        std::ofstream file(path, std::ios::binary);
        file << value;
        if (!file)
            throw std::runtime_error("Fixture write failed");
    }
    struct Projection final : SceneRuntime::SceneDocumentProjection
    {
        Editor::DocumentSession& session;
        bool fail = false, poison = false;
        int prepared = 0, committed = 0, discarded = 0;
        std::string published;
        std::function<void()> duringPrepare;
        std::function<void()> duringCommit;
        explicit Projection(Editor::DocumentSession& owner) : session(owner)
        {
            session.Document().Data()->AttachProjection(*this);
        }
        ~Projection()
        {
            session.Document().Data()->DetachProjection(*this);
        }
        struct Candidate final : Prepared
        {
            Projection& owner;
            std::string value;
            bool committed = false;
            Candidate(Projection& target, std::string json) : owner(target), value(std::move(json)) {}
            ~Candidate()
            {
                if (!committed)
                    ++owner.discarded;
            }
            void Commit() noexcept override
            {
                if (owner.duringCommit)
                    owner.duringCommit();
                owner.published.swap(value);
                ++owner.committed;
                committed = true;
            }
        };
        std::unique_ptr<Prepared> PrepareDocument(SceneRuntime::SceneEditPlan& plan, bool, std::string& error) override
        {
            ++prepared;
            if (duringPrepare)
                duringPrepare();
            if (fail)
            {
                error = "injected projection preparation failure";
                return {};
            }
            auto candidate = std::make_unique<Candidate>(*this, plan.layout.Serialize());
            if (poison && !plan.layout.objects.empty())
                plan.layout.objects.front().name.assign(1, static_cast<char>(0xff));
            return candidate;
        }
    };
    void Validate()
    {
        using namespace SceneRuntime;
        Editor::DocumentSession session;
        Projection projection(session);
        session.ResetHistory();
        std::string error;
        ScenePlacement object;
        object.id = "root";
        object.name = "Root";
        auto prepared = session.Prepare(AddObjectEdit{object}, error);
        Require(prepared && session.Layout().objects.empty() && !session.History().CanUndo() &&
                    projection.committed == 0,
                "session-prepare-keeps-source-projection-and-history");
        Require(session.Document().Data()->TakeChanges().Empty(), "session-preparation-keeps-notifications-private");
        Require(prepared->Commit(), "session-commits-document-projection-and-history");
        Require(!prepared->Commit(), "session-candidate-commits-only-once");
        Require(session.Layout().objects.size() == 1 && session.History().CanUndo() &&
                    projection.published == session.Json() && session.Editing().SelectedId() == "root",
                "session-publishes-document-display-selection-and-history");
        Require(!session.Document().Data()->TakeChanges().Empty() && session.Document().Data()->TakeChanges().Empty(),
                "session-publishes-success-notification-once");
        Require(session.Edit(NameEdit{"root", "Renamed"}, error).succeeded,
                "session-can-edit-before-old-successful-candidate-is-destroyed");
        auto next = session.Prepare(NameEdit{"root", "Next"}, error);
        prepared.reset();
        Require(next && !session.Edit(NameEdit{"root", "Reentry"}, error).succeeded,
                "session-old-candidate-destruction-does-not-unlock-new-writer");
        next.reset();
        Require(session.Layout().objects.front().name == "Renamed", "session-discarded-candidate-keeps-document");
        Require(session.RestoreHistory(false, error) && session.Layout().objects.front().name == "Root" &&
                    projection.published == session.Json(),
                "session-undo-restores-document-and-projection");
        Require(session.RestoreHistory(true, error) && session.Layout().objects.front().name == "Renamed",
                "session-redo-restores-document");
        const auto stable = session.Json();
        const auto undo = session.History().Target(false).json;
        const auto commits = projection.committed;
        projection.fail = true;
        Require(!session.Edit(NameEdit{"root", "Rejected"}, error).succeeded && session.Json() == stable &&
                    projection.committed == commits && session.History().Target(false).json == undo,
                "session-projection-failure-keeps-document-display-and-history");
        Require(!session.RestoreHistory(false, error) && session.Json() == stable &&
                    session.History().Target(false).json == undo && !session.History().CanRedo(),
                "session-failed-undo-keeps-history-cursor");
        projection.fail = false;
        projection.poison = true;
        const auto discarded = projection.discarded;
        Require(!session.Edit(NameEdit{"root", "InvalidJson"}, error).succeeded && session.Json() == stable &&
                    projection.committed == commits && projection.discarded == discarded + 1,
                "session-late-serialization-failure-discards-prepared-projection");
        projection.poison = false;
        bool rejected = false, commitProtected = false;
        projection.duringPrepare = [&]()
        {
            std::string failure;
            rejected = !session.Edit(NameEdit{"root", "Nested"}, failure).succeeded &&
                       session.Document().Data()->TakeChanges().Empty();
        };
        projection.duringCommit = [&]()
        {
            std::string failure;
            commitProtected = !session.Edit(NameEdit{"root", "CommitReentry"}, failure).succeeded &&
                              session.Document().Data()->TakeChanges().Empty();
        };
        Require(session.Edit(NameEdit{"root", "Accepted"}, error).succeeded && rejected && commitProtected,
                "session-rejects-reentry-and-notification-consumption-during-preparation-and-commit");
        projection.duringPrepare = {};
        projection.duringCommit = {};
        session.Document().Data()->TakeChanges();
        ScenePlacement anonymous;
        auto cancelled = session.Prepare(AddObjectEdit{anonymous}, error);
        Require(cancelled && cancelled->CreatedIds().size() == 1, "session-prepares-unpublished-generated-id");
        const auto generated = cancelled->CreatedIds().front();
        cancelled.reset();
        projection.fail = true;
        Require(!session.Edit(AddObjectEdit{anonymous}, error).succeeded &&
                    session.Document().Data()->TakeChanges().Empty(),
                "session-failed-add-keeps-id-and-notifications-private");
        projection.fail = false;
        auto retried = session.Prepare(AddObjectEdit{anonymous}, error);
        Require(retried && retried->CreatedIds().front() == generated && retried->Commit(),
                "session-retry-reuses-unconsumed-id");
        retried.reset();
        session.Document().Data()->TakeChanges();
        Require(session.Document().Data()->Edit(NameEdit{"root", "ThroughData"}, error).succeeded &&
                    projection.published == session.Json(),
                "session-data-edit-uses-same-owner-and-history");
        SceneDocumentData other;
        Require(!session.Document().Data()->BeginEdit(NameEdit{"root", "WrongOwner"}, error, &other),
                "session-rejects-another-editing-owner");
        session.ResetHistory();
        session.Editing().Select("root");
        session.ObserveCurrent();
        const auto shared = session.History();
        const auto beforeDrag = session.Json();
        session.Editing().SetInteraction("drag/root");
        Require(session.Edit(TransformEdit{"root", {{1, 0, 0}, {}, {1, 1, 1}}}, error).succeeded &&
                    session.Edit(TransformEdit{"root", {{2, 0, 0}, {}, {1, 1, 1}}}, error).succeeded,
                "session-groups-continuous-transform-edits");
        session.CommitInteraction();
        session.Editing().SetInteraction({});
        Require(shared.Matches(beforeDrag, "root", {"root"}, {}),
                "session-history-candidate-does-not-modify-shared-snapshots");
        Require(session.RestoreHistory(false, error) && session.Layout().objects.front().position[0] == 0 &&
                    !session.History().CanUndo() && session.Editing().SelectedId() == "root",
                "session-continuous-edit-is-one-undo-with-selection");
        Require(session.RestoreHistory(true, error) && session.Layout().objects.front().position[0] == 2,
                "session-grouped-edit-redoes-final-value");
        session.Editing().Select("root");
        session.Editing().Select(generated, true);
        session.ObserveCurrent();
        Require(session.Edit(RemoveObjectsEdit{{generated}}, error).succeeded &&
                    session.Editing().SelectedIds() == std::vector<std::string>{"root"} &&
                    session.Editing().SelectedId() == "root",
                "session-delete-primary-retains-only-surviving-selection");
        Require(session.RestoreHistory(false, error) && session.Editing().SelectedId() == generated &&
                    session.Editing().SelectedIds().size() == 2,
                "session-delete-undo-restores-primary-and-multiple-selection");
        Require(session.RestoreHistory(true, error) && session.Editing().SelectedId() == "root" &&
                    session.Editing().SelectedIds().size() == 1,
                "session-delete-redo-does-not-restore-deleted-primary");
        Require(session.Edit(RemoveObjectsEdit{{"root"}}, error).succeeded && session.Editing().SelectedIds().empty(),
                "session-delete-last-selected-object-clears-selection");
    }
    void ValidateFiles()
    {
        using namespace SceneRuntime;
        const auto root = std::filesystem::absolute("generated/tests/document-session") /
                          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::create_directories(root);
        const auto source = root / "Source.json";
        const auto target = root / "New.json";
        const auto asset = root / "Prefab.json";
        SceneLayout layout;
        ScenePlacement object;
        object.id = "root";
        object.name = "Original";
        layout.objects.push_back(object);
        Write(source, layout.Serialize());
        Write(asset, "before");
        Editor::DocumentSession session;
        Projection projection(session);
        std::string error;
        Require(session.Open(source, error) && session.Document().Path() == source && !session.History().CanUndo() &&
                    !session.Editing().HasChanges(),
                "session-open-accepts-save-baseline-and-initial-history");
        const auto stable = session.Json();
        Require(!session.Document().Open(source, error) && session.Json() == stable && !session.History().CanUndo(),
                "session-rejects-storage-open-bypassing-the-editing-owner");
        const auto sourceBytes = Read(source);
        auto changed = session.Layout();
        changed.objects.front().name = "Changed";
        auto file = Editor::FileTransaction::Prepare(asset, "after");
        projection.fail = true;
        Require(!session.ApplyFiles(changed, {file}, error) && Read(asset) == "before" && session.Json() == stable &&
                    !session.History().CanUndo(),
                "session-prefab-projection-failure-rolls-back-file-and-history");
        projection.fail = false;
        Require(session.ApplyFiles(changed, {file}, error) && Read(asset) == "after" &&
                    session.Layout().objects.front().name == "Changed" && session.History().CanUndo(),
                "session-prefab-files-document-and-history-commit-together");
        projection.fail = true;
        Require(!session.RestoreHistory(false, error) && Read(asset) == "after" &&
                    session.Layout().objects.front().name == "Changed" && !session.History().CanRedo(),
                "session-failed-prefab-undo-restores-file-and-cursor");
        projection.fail = false;
        Require(session.RestoreHistory(false, error) && Read(asset) == "before" &&
                    session.Layout().objects.front().name == "Original",
                "session-prefab-undo-restores-files-and-document");
        Require(session.RestoreHistory(true, error) && Read(asset) == "after" &&
                    session.Layout().objects.front().name == "Changed",
                "session-prefab-redo-restores-files-and-document");
        Write(asset, "external");
        Require(!session.RestoreHistory(false, error) && Read(asset) == "external" &&
                    session.Layout().objects.front().name == "Changed",
                "session-prefab-undo-rejects-external-file-change");
        projection.fail = true;
        Require(!session.Open(source, error) && session.Document().Path() == source &&
                    session.Layout().objects.front().name == "Changed",
                "session-failed-open-keeps-save-baseline-and-edits");
        projection.fail = false;
        Require(session.Open(source, error) && !session.History().CanUndo() && Read(source) == sourceBytes,
                "session-reopen-reads-source-and-resets-history");
        Require(session.Document().Request(target, true, false) && session.ApplyPending(error) &&
                    session.Document().Path() == target && session.Document().UnsavedNew() &&
                    session.Editing().HasChanges() && !std::filesystem::exists(target),
                "session-new-document-commits-unsaved-baseline-without-writing-file");
        session.Document().Save(session.Layout());
        session.MarkSaved();
        Require(std::filesystem::exists(target) && !session.Document().UnsavedNew() &&
                    !session.History().Dirty(session.Json()),
                "session-save-delegates-io-and-updates-history-baseline");
        Require(session.Document().Request(root / "Missing.json", false, false) && !session.ApplyPending(error) &&
                    session.Document().Path() == target && !session.Document().Pending(),
                "session-failed-pending-open-keeps-current-document-and-consumes-ui-request");
    }

}
int main()
{
    try
    {
        Validate();
        ValidateFiles();
        std::cout << "Document session contracts: " << contracts << ", failures: 0\n";
        return 0;
    }
    catch (const std::exception& failure)
    {
        std::cerr << "FAIL: " << failure.what() << '\n';
        return 1;
    }
}
