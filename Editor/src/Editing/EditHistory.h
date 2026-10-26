#pragma once
#include <string>
#include <vector>
#include <optional>
#include <string_view>
#include <utility>
#include <algorithm>
#include "../Documents/FileTransaction.h"

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
        /// <summary>状態を初期値へ戻します。</summary>
        void Reset(State state);
        /// <summary>素材の変更を監視対象へ追加します。</summary>
        void Observe(State state, std::string_view interaction);
        /// <summary>
        /// 編集のないフレームでは、シーンの JSON 全体をコピーせずに選択状態を更新します。
        /// </summary>
        void Observe(const std::string& json, const std::string& selection, const std::vector<std::string>& selections,
                     std::string_view interaction);
        /// <summary>準備済みの変更を確定します。</summary>
        void Commit();
        bool CanUndo() const
        {
            return cursor_ > 0;
        }
        /// <summary>シーンと素材の一つの編集を、通常の連続操作とは分けて履歴へ確定します。</summary>
        void RecordTransaction(State before, State after, std::vector<FileChange> files);
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
        /// <summary>適用済みの状態を記録します。</summary>
        void Applied(bool redo);
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
