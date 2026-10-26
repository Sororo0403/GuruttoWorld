#include "EditHistory.h"

namespace Editor
{
    auto EditHistory::Reset(State state) -> void
    {
        states_ = {std::move(state)};
        files_ = {std::vector<FileChange>{}};
        states_.reserve(102);
        files_.reserve(102);
        cursor_ = 0;
        pending_.reset();
        interaction_.clear();
        saved_ = states_[0].json;
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
        pending_ = std::move(state);
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
        auto& current = pending_ ? *pending_ : states_[cursor_];
        if (current.json == json)
        {
            current.selection = selection;
            current.selections = selections;
        }
        else
            pending_ = State{json, selection, selections};
        if (interaction.empty())
            Commit();
    }

    auto EditHistory::Commit() -> void
    {
        interaction_.clear();
        if (!pending_)
            return;
        if (pending_->json != states_[cursor_].json)
        {
            states_.resize(cursor_ + 1);
            files_.resize(cursor_ + 1);
            states_.push_back(std::move(*pending_));
            files_.emplace_back();
            if (states_.size() > 101)
            {
                states_.erase(states_.begin());
                files_.erase(files_.begin());
                files_.front().clear();
            }
            cursor_ = states_.size() - 1;
        }
        else
        {
            states_[cursor_].selection = pending_->selection;
            states_[cursor_].selections = pending_->selections;
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
        if (!changed && after.json == states_[cursor_].json)
        {
            Observe(std::move(after), "");
            return;
        }
        states_.resize(cursor_ + 1);
        files_.resize(cursor_ + 1);
        states_.push_back(std::move(after));
        files_.push_back(std::move(files));
        if (states_.size() > 101)
        {
            states_.erase(states_.begin());
            files_.erase(files_.begin());
            files_.front().clear();
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
}
