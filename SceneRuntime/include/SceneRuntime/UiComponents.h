#pragma once
#include <array>
#include <string>
#include <filesystem>
namespace SceneRuntime
{
    struct CanvasComponent
    {
        std::string id="canvas"; bool enabled=true;
        std::array<float,2> referenceSize{1280,720};
        bool operator==(const CanvasComponent&) const = default;
    };
    struct RectTransformComponent
    {
        std::string id="rect"; bool enabled=true;
        std::array<float,2> anchorMin{0,0}, anchorMax{0,0}, pivot{0,0}, position{0,0}, size{200,60};
        float rotation=0, introDelay=0, introOffset=0;
        std::string visibleWhen, offsetBinding, opacityBinding;
        bool operator==(const RectTransformComponent&) const = default;
    };
    struct ImageComponent
    {
        std::string id="image"; bool enabled=true;
        std::filesystem::path texture;
        std::array<float,4> uv{0,0,1,1}, color{1,1,1,1};
        bool operator==(const ImageComponent&) const = default;
    };
    struct TextComponent
    {
        std::string id="text"; bool enabled=true;
        std::string text="Text", font="Yu Gothic UI";
        float fontSize=32;
        std::array<float,4> color{1,1,1,1};
        bool operator==(const TextComponent&) const = default;
    };
    struct ButtonComponent
    {
        std::string id="button"; bool enabled=true;
        std::string action="click", target, sound;
        std::array<float,4> hoverColor{1,0.85f,0.8f,1}, pressedColor{0.7f,0.7f,0.7f,1};
        bool operator==(const ButtonComponent&) const = default;
    };
    struct AudioSourceComponent
    {
        std::string id="audio"; bool enabled=true;
        std::filesystem::path clip;
        float volume=1; bool loop=false, playOnAwake=false;
        std::string cue, volumeBinding;
        bool operator==(const AudioSourceComponent&) const = default;
    };
}
