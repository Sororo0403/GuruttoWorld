#include <SceneRuntime/Ui/UiGeometry.h>
#include <iostream>
#include <cmath>

#if defined(__d3d12_h__) || defined(_WINDOWS_)
#error UI geometry must not depend on a graphics device
#endif

namespace
{
    using namespace SceneRuntime;
    size_t contracts = 0;
    void Require(bool value, const char* name)
    {
        ++contracts;
        if (!value)
            throw std::runtime_error(name);
        std::cout << "PASS: " << name << std::endl;
    }
    void Validate()
    {
        UiPacket packet;
        UiNode canvas, panel, child;
        canvas.id = "canvas";
        canvas.canvas = UiCanvas{};
        canvas.canvas->referenceSize = {100, 100};
        canvas.canvas->stateDefaults = {{"visible", 1.f}, {"clock", .5f}};
        panel.id = "panel";
        panel.parentId = canvas.id;
        panel.rectTransform.emplace();
        panel.rectTransform->position = {10, 20};
        panel.rectTransform->size = {40, 30};
        panel.mask.emplace();
        child.id = "child";
        child.parentId = panel.id;
        child.rectTransform.emplace();
        child.rectTransform->position = {5, 5};
        child.rectTransform->size = {60, 20};
        child.rectTransform->visibleWhen = "visible=1";
        child.inputField.emplace();
        child.inputField->text = "日本語";
        child.inputField->password = true;
        packet.objects = {child, canvas, panel};
        const auto defaults = UiGeometry::Defaults(packet);
        Require(defaults.Value("visible") == 1 && defaults.Value("clock") == .5f,
                "ui-packet-canvas-defaults-without-device");
        const auto first = UiGeometry::Resolve(packet, packet.objects.front(), 100, 100);
        Require(first.visible && first.position == std::array<float, 2>{15, 25} &&
                    first.size == std::array<float, 2>{60, 20},
                "ui-packet-resolves-out-of-order-parents");
        Require(first.Contains(20, 30) && !first.Contains(60, 30), "ui-packet-applies-ancestor-mask");
        const auto large = UiGeometry::Resolve(packet, packet.objects.front(), 200, 200);
        Require(large.position == std::array<float, 2>{30, 50} && large.size == std::array<float, 2>{120, 40},
                "ui-packet-supports-independent-view-sizes");
        UiState hidden;
        hidden.values["visible"] = 0;
        Require(!UiGeometry::Resolve(packet, packet.objects.front(), 100, 100, hidden).visible,
                "ui-packet-respects-visibility-bindings");
        Require(UiGeometry::InputText(packet.objects.front(), {}) == "***",
                "ui-packet-password-counts-utf8-characters");
        UiState focused;
        focused.focused = child.id;
        Require(UiGeometry::InputText(packet.objects.front(), focused) == "***|",
                "ui-packet-shows-focused-input-caret");
        packet.objects.front().animation.emplace();
        AnimationTrack track;
        track.clock = "clock";
        track.property = "uiPosition";
        track.easing = "linear";
        track.keys = {{0, {5, 5, 0, 0}}, {1, {25, 15, 0, 0}}};
        packet.objects.front().animation->tracks = {track};
        const auto animated = UiGeometry::Resolve(packet, packet.objects.front(), 100, 100);
        Require(animated.position == std::array<float, 2>{25, 30}, "ui-packet-evaluates-authored-ui-animation");
        packet.objects.back().scrollView.emplace();
        packet.objects.back().scrollView->offset = {3, 4};
        const auto scrolled = UiGeometry::Resolve(packet, packet.objects.front(), 100, 100);
        Require(scrolled.position == std::array<float, 2>{22, 26}, "ui-packet-applies-parent-scroll-offset");
        packet.objects.back().layoutGroup.emplace();
        packet.objects.back().layoutGroup->direction = "grid";
        packet.objects.back().layoutGroup->cellSize = {12, 8};
        const auto arranged = UiGeometry::Resolve(packet, packet.objects.front(), 100, 100);
        Require(arranged.size == std::array<float, 2>{12, 8}, "ui-packet-applies-layout-group-cell-size");
    }
}
int main()
{
    try
    {
        Validate();
        std::cout << "UI geometry contracts: " << contracts << ", failures: 0\n";
        return 0;
    }
    catch (const std::exception& failure)
    {
        std::cerr << "FAIL: " << failure.what() << '\n';
        return 1;
    }
}
