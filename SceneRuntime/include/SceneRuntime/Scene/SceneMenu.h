#pragma once
#include <SceneRuntime/Scene/SceneLayout.h>
#include <algorithm>

namespace SceneRuntime
{
    class SceneMenu final
    {
    public:
        /// <summary>保存されたUI設定から、有効なMenu構成の所有者を読み取ります。</summary>
        static const ScenePlacement* Canvas(const SceneLayout& layout)
        {
            const auto found = std::find_if(layout.objects.begin(), layout.objects.end(),
                                            [](const auto& object)
                                            {
                                                return object.canvas && object.canvas->enabled && object.canvas->menu;
                                            });
            return found == layout.objects.end() ? nullptr : &*found;
        }
        /// <summary>任意のMenu用表示設定を取得します。実行コントローラーは生成しません。</summary>
        static const MenuConfiguration* Configuration(const SceneLayout& layout)
        {
            const auto* canvas = Canvas(layout);
            return canvas ? &*canvas->canvas->menu : nullptr;
        }
    };
}
