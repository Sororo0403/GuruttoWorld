#pragma once
#include <SceneRuntime/Scene/SceneLayout.h>
#include <unordered_map>
#include <unordered_set>

namespace Editor
{
    struct HierarchyRow
    {
        size_t index = 0, depth = 0;
        bool children = false;
    };
    /// <summary>
    /// 検証済みのシーングラフを反復処理でたどり、深い階層でもスタックを使い切らないようにします。
    /// </summary>
    std::vector<HierarchyRow> BuildHierarchyRows(const SceneRuntime::SceneLayout& layout,
                                                 const std::unordered_set<std::string>& collapsed);
}
