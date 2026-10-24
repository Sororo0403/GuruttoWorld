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
    inline void ValidateUiControl(const Engine::Json& object)
    {
        const auto check = [&](const auto& self, const Engine::Json& value) -> void
        {
            if (value.is_string())
            {
                const auto text = value.get<std::string>();
                if (text.size() > 16384 || text.find('\0') != std::string::npos)
                    throw std::runtime_error("Invalid UI control string");
            }
            else if (value.is_number())
            {
                const auto number = value.get<double>();
                if (!Engine::InRange(number, -100000, 100000))
                    throw std::runtime_error("Invalid UI control number");
            }
            else if (value.is_structured())
                for (const auto& child : value)
                    self(self, child);
        };
        check(check, object);
    }
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
    inline void ReadInputFieldControl(const Engine::Json& object, ScenePlacement& p)
    {
        if (!object.at("maxLength").is_number_integer() || object.at("maxLength").get<double>() < 1 ||
            object.at("maxLength").get<double>() > 4096)
            throw std::runtime_error("Invalid InputField length");
        ReadUiControlAs(object, p.inputField);
        if (!object.at("maxLength").is_number_unsigned() && !object.at("maxLength").is_number_integer())
            throw std::runtime_error("InputField length must be an integer");
        if (p.inputField->maxLength < 1 || p.inputField->maxLength > 4096 || p.inputField->text.size() > 16384)
            throw std::runtime_error("Invalid InputField length");
    }

    inline void ReadSliderControl(const Engine::Json& object, ScenePlacement& p)
    {
        ReadUiControlAs(object, p.slider);
        const auto& c = *p.slider;
        if (c.minimum >= c.maximum || c.value < c.minimum || c.value > c.maximum)
            throw std::runtime_error("Invalid Slider range");
        if (std::any_of(std::begin(c.fillColor), std::end(c.fillColor),
                        [](const auto value)
                        {
                            return value < 0 || value > 1;
                        }))
            throw std::runtime_error("Invalid Slider color");
    }

    inline void ReadToggleControl(const Engine::Json& object, ScenePlacement& p)
    {
        ReadUiControlAs(object, p.toggle);
        if (std::any_of(std::begin(p.toggle->checkedColor), std::end(p.toggle->checkedColor),
                        [](const auto value)
                        {
                            return value < 0 || value > 1;
                        }))
            throw std::runtime_error("Invalid Toggle color");
    }

    inline void ReadScrollViewControl(const Engine::Json& object, ScenePlacement& p)
    {
        ReadUiControlAs(object, p.scrollView);
        const auto& c = *p.scrollView;
        if (c.contentSize[0] < 0 || c.contentSize[1] < 0 || c.offset[0] < 0 || c.offset[1] < 0 || c.wheelSpeed < 0)
            throw std::runtime_error("Invalid ScrollView size");
    }

    inline void ReadLayoutGroupControl(const Engine::Json& object, ScenePlacement& p)
    {
        if (!object.at("columns").is_number_integer() || object.at("columns").get<double>() < 1 ||
            object.at("columns").get<double>() > 1024)
            throw std::runtime_error("Invalid grid columns");
        ReadUiControlAs(object, p.layoutGroup);
        const auto& c = *p.layoutGroup;
        if (c.direction != "horizontal" && c.direction != "vertical" && c.direction != "grid")
            throw std::runtime_error("Invalid layout direction");
        if (!object.at("columns").is_number_integer() || c.columns < 1 || c.columns > 1024)
            throw std::runtime_error("Invalid grid columns");
        if (std::any_of(std::begin(c.spacing), std::end(c.spacing),
                        [](const auto value)
                        {
                            return value < 0;
                        }))
            throw std::runtime_error("Negative layout spacing");
        if (std::any_of(std::begin(c.padding), std::end(c.padding),
                        [](const auto value)
                        {
                            return value < 0;
                        }))
            throw std::runtime_error("Negative layout padding");
        if (std::any_of(std::begin(c.cellSize), std::end(c.cellSize),
                        [](const auto value)
                        {
                            return value <= 0;
                        }))
            throw std::runtime_error("Invalid grid cell size");
    }
    inline bool ReadUiControl(const Engine::Json& object, ScenePlacement& p, const std::string& type)
    {
        if (type == "InputField")
            ReadInputFieldControl(object, p);
        else if (type == "Slider")
            ReadSliderControl(object, p);
        else if (type == "Toggle")
            ReadToggleControl(object, p);
        else if (type == "ScrollView")
            ReadScrollViewControl(object, p);
        else if (type == "Mask")
            ReadUiControlAs(object, p.mask);
        else if (type == "LayoutGroup")
            ReadLayoutGroupControl(object, p);
        else
            return false;
        return true;
    }
}
