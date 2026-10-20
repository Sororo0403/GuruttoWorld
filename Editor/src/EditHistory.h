#pragma once
#include <string>
#include <vector>
#include <optional>
#include <string_view>
#include <utility>
#include <algorithm>
#include "FileTransaction.h"

namespace Editor
{
    // シーンのスナップショットは削除したオブジェクト・ID・順序を保持します。GPU リソースの復元は呼び出し側が行います。
    class EditHistory final
    {
    public:
        struct State
        {
            std::string json;
            std::string selection;
            std::vector<std::string> selections{};
        };
        void Reset(State state)
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
        void Observe(State state, std::string_view interaction)
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
        /// <summary>
        /// 編集のないフレームでは、シーンの JSON 全体をコピーせずに選択状態を更新します。
        /// </summary>
        void Observe(const std::string& json, const std::string& selection, const std::vector<std::string>& selections,
                     std::string_view interaction)
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
        void Commit()
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
        bool CanUndo() const
        {
            return cursor_ > 0;
        }
        /// <summary>シーンと素材の一つの編集を、通常の連続操作とは分けて履歴へ確定します。</summary>
        void RecordTransaction(State before, State after, std::vector<FileChange> files)
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
        /// <summary>UndoまたはRedoでシーンと一緒に適用する素材の変更を返します。</summary>
        const std::vector<FileChange>& TargetFiles(bool redo) const
        {
            return files_.at(redo ? cursor_ + 1 : cursor_);
        }
        bool CanRedo() const
        {
            return cursor_ + 1 < states_.size();
        }
        const State& Target(bool redo) const
        {
            return states_.at(redo ? cursor_ + 1 : cursor_ - 1);
        }
        void Applied(bool redo)
        {
            if (redo)
                ++cursor_;
            else
                --cursor_;
            pending_.reset();
            interaction_.clear();
        }
        void Saved(const std::string& json)
        {
            Commit();
            saved_ = json;
        }
        bool Dirty(const std::string& json) const
        {
            return json != saved_;
        }

    private:
        std::vector<State> states_;
        std::vector<std::vector<FileChange>> files_;
        size_t cursor_ = 0;
        std::optional<State> pending_;
        std::string saved_;
        std::string interaction_;
    };
}
