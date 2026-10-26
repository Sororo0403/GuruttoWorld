#pragma once
#include <Engine/Core/Validation.h>
#include <iterator>
#include <algorithm>
#include <SceneRuntime/Scene/SceneLayout.h>
#include <Engine/Core/Json.h>
#include <cmath>
#include <stdexcept>
namespace SceneRuntime
{
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(InputFieldComponent, id, enabled, text, placeholder, binding, changedEvent,
                                       submittedEvent, maxLength, multiline, password, readOnly)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SliderComponent, id, enabled, minimum, maximum, value, wholeNumbers, vertical,
                                       binding, changedEvent, fillColor)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ToggleComponent, id, enabled, value, binding, changedEvent, checkedColor)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ScrollViewComponent, id, enabled, contentSize, offset, horizontal, vertical,
                                       wheelSpeed)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MaskComponent, id, enabled)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(LayoutGroupComponent, id, enabled, direction, spacing, cellSize, padding,
                                       columns, expandWidth, expandHeight)
    /// <summary>汎用UIの値と文字列の保存範囲を検証します。</summary>
    void ValidateUiControl(const Engine::Json& object);
    /// <summary>同じ型の重複を拒否してUIを読み込みます。</summary>
    template <class T> inline bool ReadUiControlAs(const Engine::Json& object, std::optional<T>& destination)
    {
        if (destination)
            throw std::runtime_error("Duplicate UI control");
        ValidateUiControl(object);
        destination = object.get<T>();
        return true;
    }
    /// <summary>汎用UIを読み込み、操作に必要な範囲を検証します。</summary>
    void ReadInputFieldControl(const Engine::Json& object, ScenePlacement& p);

    /// <summary>スライダーの設定を読み取ります。</summary>
    void ReadSliderControl(const Engine::Json& object, ScenePlacement& p);

    /// <summary>トグルの設定を読み取ります。</summary>
    void ReadToggleControl(const Engine::Json& object, ScenePlacement& p);

    /// <summary>スクロールビューの設定を読み取ります。</summary>
    void ReadScrollViewControl(const Engine::Json& object, ScenePlacement& p);

    /// <summary>レイアウトグループの設定を読み取ります。</summary>
    void ReadLayoutGroupControl(const Engine::Json& object, ScenePlacement& p);
    /// <summary>保存されたUI制御設定を読み取ります。</summary>
    bool ReadUiControl(const Engine::Json& object, ScenePlacement& p, const std::string& type);
}
