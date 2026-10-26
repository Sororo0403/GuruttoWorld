#pragma once
#include <SceneRuntime/Scene/SceneLayout.h>
#include <set>

namespace SceneRuntime
{
    /// <summary>未解決の参照も予約し、新しい個体が偶然その参照先にならないようにします。</summary>
    std::set<std::string> ObjectReferenceDomain(const SceneLayout& layout);
}
