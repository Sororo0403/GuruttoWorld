#pragma once
#include <string>
#include <vector>
#include <optional>
#include <string_view>
#include <utility>

namespace Editor
{
    // Scene snapshots keep deleted objects, IDs and ordering intact. GPU restoration is done by the caller.
    class EditHistory final
    {
    public:
        struct State { std::string json; std::string selection; std::vector<std::string> selections{}; };
        void Reset(State state) { states_={std::move(state)}; cursor_=0; pending_.reset(); interaction_.clear(); saved_=states_[0].json; }
        void Observe(State state, std::string_view interaction)
        {
            if (states_.empty()) { Reset(std::move(state)); return; }
            if (!interaction.empty() && interaction_!=interaction) Commit();
            interaction_=interaction;
            pending_=std::move(state);
            if (interaction.empty()) Commit();
        }
        void Commit()
        {
            interaction_.clear();
            if (!pending_) return;
            if (pending_->json!=states_[cursor_].json)
            {
                states_.resize(cursor_+1);
                states_.push_back(std::move(*pending_));
                if (states_.size()>101) states_.erase(states_.begin());
                cursor_=states_.size()-1;
            }
            else
            {
                states_[cursor_].selection=pending_->selection;
                states_[cursor_].selections=pending_->selections;
            }
            pending_.reset();
        }
        bool CanUndo() const { return cursor_>0; }
        bool CanRedo() const { return cursor_+1<states_.size(); }
        const State& Target(bool redo) const { return states_.at(redo ? cursor_+1 : cursor_-1); }
        void Applied(bool redo) { if (redo) ++cursor_; else --cursor_; pending_.reset(); interaction_.clear(); }
        void Saved(const std::string& json) { Commit(); saved_=json; }
        bool Dirty(const std::string& json) const { return json!=saved_; }
    private:
        std::vector<State> states_;
        size_t cursor_=0;
        std::optional<State> pending_;
        std::string saved_;
        std::string interaction_;
    };
}
