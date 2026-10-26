#pragma once
#include "UiGeometryFunctions.h"
#include <Engine/Graphics/Renderers/SpriteRenderer.h>

namespace SceneRuntime::UiVisualDetail
{
    /// <summary>背景を補う必要があるUIコントロールかを返します。</summary>
    bool HasControlBackground(const auto& p)
    {
        return (p.slider && p.slider->enabled) || (p.toggle && p.toggle->enabled) ||
               (p.inputField && p.inputField->enabled);
    }
    /// <summary>入力と選択状態を描画用の文字・色へ反映します。</summary>
    template <class Node> Node ControlVisual(const Node& source, const UiState& state)
    {
        auto p = source;
        if (HasControlBackground(p))
        {
            if (!p.image)
            {
                p.image.emplace();
                p.image->color = {.16f, .18f, .22f, 1};
            }
        }
        if (p.inputField && p.inputField->enabled)
        {
            if (!p.text)
            {
                p.text.emplace();
                p.text->fontSize = 24;
            }
            p.text->text = UiGeometryDetail::InputText(p, state);
            if (state.focused == p.id)
                p.image->color = {.2f, .25f, .35f, 1};
        }
        if (p.toggle && p.toggle->enabled)
        {
            const auto& c = *p.toggle;
            if (state.Value(c.binding.empty() ? p.id : c.binding, c.value ? 1.0f : 0.0f) != 0)
                p.image->color = c.checkedColor;
        }
        return p;
    }
    /// <summary>スプライトの定数へ切り抜き領域を渡します。</summary>
    inline void SetClip(Engine::SpriteDrawParameters& draw, const UiRect& rect)
    {
        std::copy(rect.clip.begin(), rect.clip.end(), draw.pixelConstants.begin() + 16);
    }
    /// <summary>値に応じたスライダーの塗り領域を取得します。</summary>
    UiRect SliderFill(const auto& p, const UiRect& rect, const UiState& state)
    {
        auto filled = rect;
        const auto& c = *p.slider;
        const float ratio = std::clamp((state.Value(c.binding.empty() ? p.id : c.binding, c.value) - c.minimum) /
                                           (c.maximum - c.minimum),
                                       0.0f, 1.0f);
        const size_t axis = c.vertical ? 1 : 0;
        filled.size[axis] *= ratio;
        const float delta = (filled.size[axis] - rect.size[axis]) * .5f;
        filled.position[0] +=
            (axis == 0 ? std::cos(rect.rotation) : -std::sin(rect.rotation)) * delta - delta * (axis == 0 ? 1 : 0);
        filled.position[1] +=
            (axis == 0 ? std::sin(rect.rotation) : std::cos(rect.rotation)) * delta - delta * (axis == 1 ? 1 : 0);
        return filled;
    }
}
