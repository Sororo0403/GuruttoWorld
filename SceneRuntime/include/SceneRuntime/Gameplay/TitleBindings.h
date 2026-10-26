#pragma once
#include <iterator>
#include <SceneRuntime/Gameplay/TitleMenu.h>
#include <SceneRuntime/Ui/SceneUi.h>
#include <optional>
#include <algorithm>
#include <charconv>
#include <Engine/Input/InputActions.h>

namespace SceneRuntime
{
    struct TitleBindings
    {
        /// <summary>アナログ軸をアクション値へ変換します。</summary>
        static float AnalogValue(const Engine::InputBinding& binding, const Engine::InputSnapshot* snapshot);

        /// <summary>入力機器に応じた割り当てを反映します。</summary>
        static void ApplyDeviceBinding(TitleMenuInput& input, const Engine::InputBinding& binding,
                                       const Engine::InputSnapshot* snapshot, unsigned int button);

        /// <summary>現在の入力を取得します。</summary>
        static TitleMenuInput Input(const SceneRuntime::MenuConfiguration& config, const Engine::InputActions& actions,
                                    bool active, const Engine::InputSnapshot* snapshot = nullptr);
        /// <summary>対象の項目を取得します。</summary>
        static std::optional<TitleMenuItem> Item(const SceneRuntime::ScenePlacement& object);
        /// <summary>メニューが使用するCanvasを取得します。</summary>
        static const SceneRuntime::ScenePlacement* MenuCanvas(const SceneRuntime::SceneLayout& layout);
        static const SceneRuntime::MenuConfiguration* Configuration(const SceneRuntime::SceneLayout& layout)
        {
            const auto* object = MenuCanvas(layout);
            return object ? &*object->canvas->menu : nullptr;
        }
        /// <summary>処理対象の項目を収集します。</summary>
        static std::vector<TitleMenuItem> Items(const SceneRuntime::SceneLayout& layout);
        /// <summary>Canvasの基準サイズを求めます。</summary>
        static std::array<unsigned int, 2> ReferenceSize(const SceneRuntime::SceneLayout& layout);
    };
}
