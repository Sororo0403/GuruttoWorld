#pragma once
#include <algorithm>
#include <string>
#include <vector>
#include <iterator>

namespace Editor
{
    class SelectionSet final
    {
    public:
        const std::vector<std::string>& Ids() const
        {
            return ids_;
        }
        const std::string& Primary() const
        {
            return ids_.empty() ? empty_ : ids_.back();
        }
        bool Contains(const std::string& id) const
        {
            return std::find(ids_.begin(), ids_.end(), id) != ids_.end();
        }
        /// <summary>指定した対象を選択します。</summary>
        void Select(std::string id, bool additive = false);
        /// <summary>変更前の状態へ戻します。</summary>
        void Restore(const std::vector<std::string>& ids, std::string primary);
        /// <summary>対象の範囲を求めます。</summary>
        void Range(const std::vector<std::string>& visible, const std::string& anchor, const std::string& clicked,
                   bool additive);

        /// <summary>準備した選択を確保なしで交換します。</summary>
        void Swap(SelectionSet& candidate) noexcept
        {
            ids_.swap(candidate.ids_);
        }

    private:
        std::vector<std::string> ids_;
        inline static const std::string empty_{};
    };
}
