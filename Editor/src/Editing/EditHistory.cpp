#include "EditHistory.h"

namespace Editor
{
    auto EditHistory::Reset(State state) -> void
    {
        states_ = {std::make_shared<State>(std::move(state))};
        files_ = {std::make_shared<const std::vector<FileChange>>()};
        states_.reserve(102);
        files_.reserve(102);
        cursor_ = 0;
        pending_.reset();
        interaction_.clear();
        saved_ = std::make_shared<const std::string>(states_[0]->json);
    }

    auto EditHistory::Observe(State state, std::string_view interaction) -> void
    {
        if (states_.empty())
        {
            Reset(std::move(state));
            return;
        }
        if (!interaction.empty() && interaction_ != interaction)
            Commit();
        interaction_ = interaction;
        pending_ = std::make_shared<State>(std::move(state));
        if (interaction.empty())
            Commit();
    }

    auto EditHistory::Observe(const std::string& json, const std::string& selection,
                              const std::vector<std::string>& selections, std::string_view interaction) -> void
    {
        if (states_.empty())
        {
            Reset({json, selection, selections});
            return;
        }
        if (!interaction.empty() && interaction_ != interaction)
            Commit();
        interaction_ = interaction;
        const auto& current = pending_ ? *pending_ : *states_[cursor_];
        if (current.json == json)
        {
            if (current.selection != selection || current.selections != selections)
            {
                auto& candidate = MutableCurrent();
                candidate.selection = selection;
                candidate.selections = selections;
            }
        }
        else
            pending_ = std::make_shared<State>(State{json, selection, selections});
        if (interaction.empty())
            Commit();
    }

    auto EditHistory::Commit() -> void
    {
        interaction_.clear();
        if (!pending_)
            return;
        if (pending_->json != states_[cursor_]->json)
        {
            states_.resize(cursor_ + 1);
            files_.resize(cursor_ + 1);
            states_.push_back(std::move(pending_));
            files_.push_back(std::make_shared<const std::vector<FileChange>>());
            if (states_.size() > 101)
            {
                states_.erase(states_.begin());
                files_.erase(files_.begin());
                files_.front() = std::make_shared<const std::vector<FileChange>>();
            }
            cursor_ = states_.size() - 1;
        }
        else
        {
            if (states_[cursor_].use_count() != 1)
                states_[cursor_] = std::make_shared<State>(*states_[cursor_]);
            states_[cursor_]->selection = pending_->selection;
            states_[cursor_]->selections = pending_->selections;
        }
        pending_.reset();
    }

    auto EditHistory::RecordTransaction(State before, State after, std::vector<FileChange> files) -> void
    {
        Observe(std::move(before), "");
        Commit();
        const bool changed = std::any_of(files.begin(), files.end(),
                                         [](const auto& file)
                                         {
                                             return file.before != file.after;
                                         });
        if (!changed && after.json == states_[cursor_]->json)
        {
            Observe(std::move(after), "");
            return;
        }
        states_.resize(cursor_ + 1);
        files_.resize(cursor_ + 1);
        states_.push_back(std::make_shared<State>(std::move(after)));
        files_.push_back(std::make_shared<const std::vector<FileChange>>(std::move(files)));
        if (states_.size() > 101)
        {
            states_.erase(states_.begin());
            files_.erase(files_.begin());
            files_.front() = std::make_shared<const std::vector<FileChange>>();
        }
        cursor_ = states_.size() - 1;
    }

    auto EditHistory::Applied(bool redo) -> void
    {
        if (redo)
            ++cursor_;
        else
            --cursor_;
        pending_.reset();
        interaction_.clear();
    }
    EditHistory::State& EditHistory::MutableCurrent()
    {
        auto& current = pending_ ? pending_ : states_[cursor_];
        if (current.use_count() != 1)
            current = std::make_shared<State>(*current);
        return *current;
    }
    void EditHistory::Swap(EditHistory& candidate) noexcept
    {
        states_.swap(candidate.states_);
        files_.swap(candidate.files_);
        pending_.swap(candidate.pending_);
        saved_.swap(candidate.saved_);
        interaction_.swap(candidate.interaction_);
        std::swap(cursor_, candidate.cursor_);
    }
    bool EditHistory::Matches(const std::string& json, const std::string& selection,
                              const std::vector<std::string>& selections, std::string_view interaction) const
    {
        if (states_.empty() || interaction_ != interaction || (interaction.empty() && pending_))
            return false;
        const auto& current = pending_ ? *pending_ : *states_[cursor_];
        return current.json == json && current.selection == selection && current.selections == selections;
    }

}
