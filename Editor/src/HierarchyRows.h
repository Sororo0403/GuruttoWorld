#pragma once
#include <SceneRuntime/SceneLayout.h>
#include <unordered_map>
#include <unordered_set>

namespace Editor
{
    struct HierarchyRow { size_t index=0, depth=0; bool children=false; };
    // Validated scene graph, traversed iteratively so deep hierarchies do not exhaust the stack.
    inline std::vector<HierarchyRow> BuildHierarchyRows(const SceneRuntime::SceneLayout& layout,
        const std::unordered_set<std::string>& collapsed)
    {
        std::unordered_map<std::string,std::vector<size_t>> children;
        for (size_t index=0;index<layout.objects.size();++index) children[layout.objects[index].parentId].push_back(index);
        std::vector<HierarchyRow> stack,rows;
        for (auto item=children[""].rbegin();item!=children[""].rend();++item) stack.push_back({*item,0,false});
        while (!stack.empty())
        {
            auto row=stack.back(); stack.pop_back();
            const auto& object=layout.objects[row.index];
            const auto found=children.find(object.id);
            row.children=found!=children.end();
            rows.push_back(row);
            if (!row.children || collapsed.contains(object.id)) continue;
            for (auto item=found->second.rbegin();item!=found->second.rend();++item) stack.push_back({*item,row.depth+1,false});
        }
        return rows;
    }
}
