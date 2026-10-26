#include "UiControlJson.h"

namespace SceneRuntime
{
    void ValidateUiControl(const Engine::Json& object)
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

    void ReadInputFieldControl(const Engine::Json& object, ScenePlacement& p)
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

    void ReadSliderControl(const Engine::Json& object, ScenePlacement& p)
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

    void ReadToggleControl(const Engine::Json& object, ScenePlacement& p)
    {
        ReadUiControlAs(object, p.toggle);
        if (std::any_of(std::begin(p.toggle->checkedColor), std::end(p.toggle->checkedColor),
                        [](const auto value)
                        {
                            return value < 0 || value > 1;
                        }))
            throw std::runtime_error("Invalid Toggle color");
    }

    void ReadScrollViewControl(const Engine::Json& object, ScenePlacement& p)
    {
        ReadUiControlAs(object, p.scrollView);
        const auto& c = *p.scrollView;
        if (c.contentSize[0] < 0 || c.contentSize[1] < 0 || c.offset[0] < 0 || c.offset[1] < 0 || c.wheelSpeed < 0)
            throw std::runtime_error("Invalid ScrollView size");
    }

    void ReadLayoutGroupControl(const Engine::Json& object, ScenePlacement& p)
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

    bool ReadUiControl(const Engine::Json& object, ScenePlacement& p, const std::string& type)
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
