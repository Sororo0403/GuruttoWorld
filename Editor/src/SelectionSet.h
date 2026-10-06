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
        const std::vector<std::string>& Ids() const { return ids_; }
        const std::string& Primary() const { return ids_.empty() ? empty_ : ids_.back(); }
        bool Contains(const std::string& id) const { return std::find(ids_.begin(),ids_.end(),id)!=ids_.end(); }
        void Select(std::string id, bool additive=false)
        {
            if (!additive) ids_.clear();
            if (id.empty()) return;
            const auto found=std::find(ids_.begin(),ids_.end(),id);
            if (found==ids_.end()) ids_.push_back(std::move(id)); else ids_.erase(found);
        }
        void Restore(const std::vector<std::string>& ids, std::string primary)
        {
            const auto snapshot=ids;
            ids_.clear();
            std::copy_if(snapshot.begin(),snapshot.end(),std::back_inserter(ids_),
                [&](const auto& id) { return !id.empty() && !Contains(id); });
            if (!primary.empty())
            {
                std::erase(ids_,primary);
                ids_.push_back(std::move(primary));
            }
        }
        void Range(const std::vector<std::string>& visible, const std::string& anchor,
            const std::string& clicked, bool additive)
        {
            const auto first=std::find(visible.begin(),visible.end(),anchor);
            const auto last=std::find(visible.begin(),visible.end(),clicked);
            if (first==visible.end() || last==visible.end()) { Select(clicked,additive); return; }
            if (!additive) ids_.clear();
            for (auto item=std::min(first,last);item!=std::max(first,last)+1;++item)
                if (!Contains(*item)) ids_.push_back(*item);
            std::erase(ids_,clicked);
            ids_.push_back(clicked);
        }
    private:
        std::vector<std::string> ids_;
        inline static const std::string empty_{};
    };
}
