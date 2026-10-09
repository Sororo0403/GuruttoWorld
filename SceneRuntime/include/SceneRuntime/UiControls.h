#pragma once
#include <array>
#include <string>
namespace SceneRuntime
{
    struct InputFieldComponent
    {
        std::string id="inputField"; bool enabled=true;
        std::string text, placeholder="Enter text", binding, changedEvent, submittedEvent;
        unsigned int maxLength=256;
        bool multiline=false, password=false, readOnly=false;
        bool operator==(const InputFieldComponent&) const = default;
    };
    struct SliderComponent
    {
        std::string id="slider"; bool enabled=true;
        float minimum=0, maximum=1, value=0;
        bool wholeNumbers=false, vertical=false;
        std::string binding, changedEvent;
        std::array<float,4> fillColor{0.25f,0.65f,1,1};
        bool operator==(const SliderComponent&) const = default;
    };
    struct ToggleComponent
    {
        std::string id="toggle"; bool enabled=true, value=false;
        std::string binding, changedEvent;
        std::array<float,4> checkedColor{0.25f,0.85f,0.4f,1};
        bool operator==(const ToggleComponent&) const = default;
    };
    struct ScrollViewComponent
    {
        std::string id="scrollView"; bool enabled=true;
        std::array<float,2> contentSize{400,800}, offset{};
        bool horizontal=false, vertical=true;
        float wheelSpeed=40;
        bool operator==(const ScrollViewComponent&) const = default;
    };
    struct MaskComponent
    {
        std::string id="mask"; bool enabled=true;
        bool operator==(const MaskComponent&) const = default;
    };
    struct LayoutGroupComponent
    {
        std::string id="layoutGroup"; bool enabled=true;
        std::string direction="vertical";
        std::array<float,2> spacing{8,8}, cellSize{100,40};
        std::array<float,4> padding{8,8,8,8};
        unsigned int columns=2;
        bool expandWidth=false, expandHeight=false;
        bool operator==(const LayoutGroupComponent&) const = default;
    };
}
