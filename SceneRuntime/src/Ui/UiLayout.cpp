#include <Engine/Core/Validation.h>
#include <SceneRuntime/Ui/SceneUi.h>
#include "UiGeometryFunctions.h"
#include <iterator>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
namespace SceneRuntime
{
    namespace
    {
        using namespace UiGeometryDetail;
        bool PersistentUi(const SceneLayout& previousLayout, const ScenePlacement& old)
        {
            const auto* ancestor = &old;
            bool persistent = false;
            size_t remaining = previousLayout.objects.size() + 1;
            while (ancestor && remaining-- > 0)
            {
                if (ancestor->persistent)
                {
                    persistent = true;
                    break;
                }
                ancestor = Find(previousLayout, ancestor->parentId);
            }
            return persistent;
        }

        void CopyNumericState(const UiState& previous, UiState& next, const ScenePlacement& old,
                              const ScenePlacement* current, const std::string& oldBinding, const std::string& binding)
        {
            const auto found = previous.values.find(oldBinding.empty() ? old.id : oldBinding);
            if (found != previous.values.end())
                next.values[binding.empty() ? current->id : binding] = found->second;
        }
        void CopyPersistentUi(const ScenePlacement& old, const ScenePlacement* current, const UiState& previous,
                              UiState& next)
        {
            if (old.slider && current->slider)
                CopyNumericState(previous, next, old, current, old.slider->binding, current->slider->binding);
            if (old.toggle && current->toggle)
                CopyNumericState(previous, next, old, current, old.toggle->binding, current->toggle->binding);
            if (old.inputField && current->inputField)
            {
                const auto found =
                    previous.strings.find(old.inputField->binding.empty() ? old.id : old.inputField->binding);
                if (found != previous.strings.end())
                    next.strings[current->inputField->binding.empty() ? current->id : current->inputField->binding] =
                        found->second;
            }
            if (old.scrollView && current->scrollView)
            {
                const auto found = previous.scrollOffsets.find(old.id);
                if (found != previous.scrollOffsets.end())
                    next.scrollOffsets[current->id] = found->second;
            }
        }

        void ClearInvalidFocus(const SceneLayout& layout, UiState& next)
        {
            const auto* focused = Find(layout, next.focused);
            if (!focused || !focused->inputField || !focused->inputField->enabled ||
                !SceneUi::Resolve(layout, *focused, 1, 1, next).visible)
                next.focused.clear();
        }

        UiEvent ActivateButton(const ScenePlacement& object, UiState& state)
        {
            const auto* p = &object;
            const auto& b = *p->button;
            if (b.action == "show")
                state.visibility[b.target] = true;
            if (b.action == "hide")
                state.visibility[b.target] = false;
            if (b.action == "toggle")
            {
                const auto i = state.visibility.find(b.target);
                state.visibility[b.target] = !(i == state.visibility.end() || i->second);
            }
            if (b.action == "setState")
                state.Assign(b.target);
            return {p->id, b.action, b.target, b.sound, b.event};
        }

        void PressPointer(const SceneLayout& layout, const UiPointerInput& input, UiState& state)
        {
            const auto& [x, y, down, pressed, released, wheel] = input;
            if (pressed)
            {
                state.pressed = state.hovered;
                state.dragPosition = {x, y};
                const auto* p = Find(layout, state.pressed);
                if (!p || !p->inputField || !p->inputField->enabled)
                    state.focused.clear();
                else
                    state.focused = p->id;
                if (p && p->scrollView)
                    state.dragScroll =
                        state.scrollOffsets.contains(p->id) ? state.scrollOffsets.at(p->id) : p->scrollView->offset;
            }
        }

        UiEvent UpdateSlider(const SceneLayout& layout, unsigned int width, unsigned int height,
                             const ScenePlacement* p, const UiPointerInput& input, UiState& state)
        {
            UiEvent event;
            const auto& [x, y, down, pressed, released, wheel] = input;
            if (p && p->slider && p->slider->enabled && down)
            {
                const auto& c = *p->slider;
                const auto rect = SceneUi::Resolve(layout, *p, width, height, state);
                const float localX = x - rect.position[0] - rect.size[0] * .5f,
                            localY = y - rect.position[1] - rect.size[1] * .5f;
                const size_t axis = c.vertical ? 1 : 0;
                const float local = axis == 0 ? localX * std::cos(rect.rotation) + localY * std::sin(rect.rotation)
                                              : -localX * std::sin(rect.rotation) + localY * std::cos(rect.rotation);
                const float t = rect.size[axis] > 0 ? std::clamp(local / rect.size[axis] + .5f, 0.0f, 1.0f) : 0;
                float value = c.minimum + (c.maximum - c.minimum) * t;
                if (c.wholeNumbers)
                    value = std::round(value);
                value = std::clamp(value, c.minimum, c.maximum);
                const auto key = c.binding.empty() ? p->id : c.binding;
                if (state.Value(key, c.value) != value)
                {
                    state.values[key] = value;
                    event = {p->id, "valueChanged", key, {}, c.changedEvent, value, {}};
                }
            }
            return event;
        }

        void ScrollPointer(const SceneLayout& layout, unsigned int width, unsigned int height,
                           const ScenePlacement& owner, const UiPointerInput& input, UiState& state, bool dragging)
        {
            const auto& [x, y, down, pressed, released, wheel] = input;

            const auto& c = *owner.scrollView;
            const auto rect = SceneUi::Resolve(layout, owner, width, height, state);
            auto& offset = state.scrollOffsets.try_emplace(owner.id, c.offset).first->second;
            const float dx = state.dragPosition[0] - x, dy = state.dragPosition[1] - y;
            const std::array<float, 2> localDrag{dx * std::cos(rect.rotation) + dy * std::sin(rect.rotation),
                                                 -dx * std::sin(rect.rotation) + dy * std::cos(rect.rotation)};
            for (size_t axis = 0; axis < 2; ++axis)
            {
                if (!(axis == 0 ? c.horizontal : c.vertical))
                {
                    offset[axis] = 0;
                    continue;
                }
                const float maximum = std::max(0.0f, c.contentSize[axis] - rect.size[axis] / rect.scale);
                const float value = dragging ? state.dragScroll[axis] + localDrag[axis] / rect.scale
                                             : offset[axis] - ((axis == 1 || !c.vertical) ? wheel * c.wheelSpeed : 0);
                offset[axis] = std::clamp(value, 0.0f, maximum);
            }
        }

        void WheelScroll(const SceneLayout& layout, unsigned int width, unsigned int height,
                         const UiPointerInput& input, UiState& state)
        {
            const auto& [x, y, down, pressed, released, wheel] = input;
            if (wheel != 0)
            {
                const auto* hovered = Find(layout, SceneUi::Hit(layout, width, height, x, y, state, false));
                while (hovered)
                {
                    if (hovered->scrollView && hovered->scrollView->enabled)
                    {
                        ScrollPointer(layout, width, height, *hovered, input, state, false);
                        break;
                    }
                    hovered = Find(layout, hovered->parentId);
                }
            }
        }

        bool PrintableCharacter(char32_t character)
        {
            return (character >= 32 || character == '\n') && character <= 0x10ffff &&
                   !(character >= 0xd800 && character <= 0xdfff);
        }
        void EraseLastCharacter(std::string& text)
        {
            if (!text.empty())
            {
                size_t index = text.size() - 1;
                while (index > 0 && (static_cast<unsigned char>(text[index]) & 0xc0) == 0x80)
                    --index;
                text.erase(index);
            }
        }

        bool AppendCharacter(char32_t character, size_t maximum, std::string& text)
        {
            const auto count = static_cast<size_t>(std::count_if(text.begin(), text.end(),
                                                                 [](const unsigned char byte)
                                                                 {
                                                                     return (byte & 0xc0) != 0x80;
                                                                 }));
            if (count >= maximum)
                return false;
            if (character <= 0x7f)
                text += static_cast<char>(character);
            else if (character <= 0x7ff)
            {
                text += static_cast<char>(0xc0 | (character >> 6));
                text += static_cast<char>(0x80 | (character & 0x3f));
            }
            else if (character <= 0xffff)
            {
                text += static_cast<char>(0xe0 | (character >> 12));
                text += static_cast<char>(0x80 | ((character >> 6) & 0x3f));
                text += static_cast<char>(0x80 | (character & 0x3f));
            }
            else
            {
                text += static_cast<char>(0xf0 | (character >> 18));
                text += static_cast<char>(0x80 | ((character >> 12) & 0x3f));
                text += static_cast<char>(0x80 | ((character >> 6) & 0x3f));
                text += static_cast<char>(0x80 | (character & 0x3f));
            }
            return true;
        }

    }
    UiState SceneUi::Defaults(const SceneLayout& layout)
    {
        return UiGeometryDetail::DefaultState(layout);
    }
    UiState SceneUi::SceneState(const SceneLayout& previousLayout, const SceneLayout& layout, const UiState& previous,
                                bool reset)
    {
        auto next = reset ? Defaults(layout) : previous;
        if (!reset)
        {
            const auto defaults = Defaults(layout);
            next.values.insert(defaults.values.begin(), defaults.values.end());
            next.strings.insert(defaults.strings.begin(), defaults.strings.end());
            next.scrollOffsets.insert(defaults.scrollOffsets.begin(), defaults.scrollOffsets.end());
        }
        else
        {
            for (const auto& old : previousLayout.objects)
            {
                const bool persistent = PersistentUi(previousLayout, old);
                const auto* current = Find(layout, old.id);
                if (!persistent || !current)
                    continue;
                CopyPersistentUi(old, current, previous, next);
            }
        }
        next.hovered.clear();
        next.pressed.clear();
        ClearInvalidFocus(layout, next);
        return next;
    }
    UiRect SceneUi::Resolve(const SceneLayout& layout, const ScenePlacement& object, unsigned int width,
                            unsigned int height, const UiState& state)
    {
        return UiGeometryDetail::Resolve(layout, object, width, height, state);
    }
    std::string SceneUi::Hit(const SceneLayout& layout, unsigned int width, unsigned int height, float x, float y,
                             const UiState& state, bool buttonsOnly)
    {
        for (auto i = layout.objects.rbegin(); i != layout.objects.rend(); ++i)
        {
            if (!i->rectTransform ||
                (buttonsOnly && !((i->button && i->button->enabled) || (i->inputField && i->inputField->enabled) ||
                                  (i->slider && i->slider->enabled) || (i->toggle && i->toggle->enabled) ||
                                  (i->scrollView && i->scrollView->enabled))))
                continue;
            if (Resolve(layout, *i, width, height, state).Contains(x, y))
                return i->id;
        }
        return {};
    }
    UiEvent SceneUi::Activate(const SceneLayout& layout, const std::string& object, UiState& state)
    {
        const auto* p = Find(layout, object);
        if (!p)
            return {};
        if (p->toggle && p->toggle->enabled)
        {
            const auto& c = *p->toggle;
            const auto key = c.binding.empty() ? p->id : c.binding;
            const float value = state.Value(key, c.value ? 1.0f : 0.0f) == 0 ? 1.0f : 0.0f;
            state.values[key] = value;
            return {p->id, "valueChanged", key, {}, c.changedEvent, value, {}};
        }
        if (p->inputField && p->inputField->enabled)
        {
            state.focused = p->id;
            return {};
        }
        if (!p->button || !p->button->enabled)
            return {};
        return ActivateButton(*p, state);
    }
    UiEvent SceneUi::Pointer(const SceneLayout& layout, unsigned int width, unsigned int height,
                             const UiPointerInput& input, UiState& state)
    {
        const auto& [x, y, down, pressed, released, wheel] = input;
        state.hovered = Hit(layout, width, height, x, y, state);
        PressPointer(layout, input, state);
        UiEvent event;
        const auto* p = Find(layout, state.pressed);
        event = UpdateSlider(layout, width, height, p, input, state);

        if (p && p->scrollView && p->scrollView->enabled && down)
            ScrollPointer(layout, width, height, *p, input, state, true);
        WheelScroll(layout, width, height, input, state);
        if (released)
        {
            if (!state.pressed.empty() && state.pressed == state.hovered)
                event = Activate(layout, state.pressed, state);
            state.pressed.clear();
        }
        return event;
    }
    std::string SceneUi::InputText(const ScenePlacement& object, const UiState& state)
    {
        return UiGeometryDetail::InputText(object, state);
    }
    UiEvent SceneUi::TextInput(const SceneLayout& layout, char32_t character, UiState& state)
    {
        const auto* p = Find(layout, state.focused);
        if (!p || !p->inputField || !p->inputField->enabled)
            return {};
        const auto& c = *p->inputField;
        const auto key = c.binding.empty() ? p->id : c.binding;
        auto& text = state.strings.try_emplace(key, c.text).first->second;
        if (character == 27)
        {
            state.focused.clear();
            return {};
        }
        if (character == '\r' || character == '\n')
        {
            if (!c.multiline)
                return {p->id, "submit", key, {}, c.submittedEvent, 0, text};
            character = '\n';
        }
        if (c.readOnly)
            return {};
        const auto before = text;
        if (character == 8)
        {
            EraseLastCharacter(text);
        }
        else if (PrintableCharacter(character))
        {
            if (!AppendCharacter(character, c.maxLength, text))
                return {};
        }
        return text == before ? UiEvent{} : UiEvent{p->id, "valueChanged", key, {}, c.changedEvent, 0, text};
    }
}
