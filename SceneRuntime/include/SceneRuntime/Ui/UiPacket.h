#pragma once
#include <SceneRuntime/Ui/UiComponents.h>
#include <SceneRuntime/Animation/Animation.h>
#include <DirectXMath.h>
#include <optional>
#include <vector>

namespace SceneRuntime
{
    struct UiCanvas
    {
        bool enabled = true;
        std::array<float, 2> referenceSize{1280, 720};
        bool scaleWithScreen = true;
        std::map<std::string, float> stateDefaults;
    };
    struct UiNode
    {
        std::string id, parentId;
        DirectX::XMFLOAT4X4 matrix{};
        std::optional<UiCanvas> canvas;
        std::optional<RectTransformComponent> rectTransform;
        std::optional<ImageComponent> image;
        std::optional<TextComponent> text;
        std::optional<ButtonComponent> button;
        std::optional<InputFieldComponent> inputField;
        std::optional<SliderComponent> slider;
        std::optional<ToggleComponent> toggle;
        std::optional<ScrollViewComponent> scrollView;
        std::optional<MaskComponent> mask;
        std::optional<LayoutGroupComponent> layoutGroup;
        std::optional<AnimationComponent> animation;
    };
    struct UiPacket
    {
        std::vector<UiNode> objects;
    };
}
