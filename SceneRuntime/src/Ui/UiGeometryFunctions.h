#pragma once
#include <SceneRuntime/Ui/UiGeometry.h>
#include <algorithm>
#include <cmath>

namespace SceneRuntime::UiGeometryDetail
{
    /// <summary>親の保存IDから、同じUI定義内のノードを取得します。</summary>
    auto Find(const auto& layout, const std::string& id)
    {
        const auto i = std::find_if(layout.objects.begin(), layout.objects.end(),
                                    [&](const auto& p)
                                    {
                                        return p.id == id;
                                    });
        return i == layout.objects.end() ? nullptr : &*i;
    }
    /// <summary>親の配置へアンカー・回転・状態値を反映します。</summary>
    inline UiRect Child(const UiRect& parent, const RectTransformComponent& c, const UiState& state)
    {
        UiRect r = parent;
        r.rotation += c.rotation;
        r.visible = r.visible && c.enabled && state.Matches(c.visibleWhen);
        const float progress = std::clamp((state.Value("intro", 1) - c.introDelay) / (1 - c.introDelay), 0.0f, 1.0f);
        const float ease = 1 - std::pow(1 - progress, 3.0f);
        const float offset = c.introOffset * (1 - ease) + state.Value(c.offsetBinding);
        std::array<float, 2> center{};
        for (size_t i = 0; i < 2; ++i)
        {
            r.size[i] = parent.size[i] * (c.anchorMax[i] - c.anchorMin[i]) + c.size[i] * r.scale;
            if (i == 0 && !c.widthBinding.empty())
                r.size[i] *= std::clamp(state.Value(c.widthBinding), 0.0f, 1.0f);
            center[i] = parent.size[i] * c.anchorMin[i] + (c.position[i] + (i == 0 ? offset : 0)) * r.scale +
                        r.size[i] * (0.5f - c.pivot[i]) - parent.size[i] * 0.5f;
        }
        r.position = {parent.position[0] + parent.size[0] * 0.5f + center[0] * std::cos(parent.rotation) -
                          center[1] * std::sin(parent.rotation) - r.size[0] * 0.5f,
                      parent.position[1] + parent.size[1] * 0.5f + center[0] * std::sin(parent.rotation) +
                          center[1] * std::cos(parent.rotation) - r.size[1] * 0.5f};
        r.opacity *= ease * state.Value(c.opacityBinding, 1);
        return r;
    }
    /// <summary>UI用のアニメーション値を配置へ適用します。</summary>
    inline void ApplyRectTrack(RectTransformComponent& rect, const AnimationTrack& track,
                               const std::array<float, 4>& value, float& opacity)
    {
        if (track.property == "uiPosition")
            rect.position = {value[0], value[1]};
        else if (track.property == "uiSize")
            rect.size = {std::max(0.0f, value[0]), std::max(0.0f, value[1])};
        else if (track.property == "uiRotation")
            rect.rotation = value[0];
        else if (track.property == "opacity")
            opacity = std::clamp(value[0], 0.0f, 1.0f);
    }
    /// <summary>指定したUI状態の時計から配置のアニメーションを評価します。</summary>
    RectTransformComponent AnimatedRect(const auto& node, const UiState& state, float& opacity)
    {
        auto rect = *node.rectTransform;
        if (!node.animation || !node.animation->enabled)
            return rect;
        for (const auto& track : node.animation->tracks)
        {
            if (track.clock == "motionTime" && state.Value(track.clock, -1) < track.delay)
                continue;
            const auto value = Animation::Sample(track, state.values);
            if (value)
                ApplyRectTrack(rect, track, *value, opacity);
        }
        return rect;
    }
    /// <summary>レイアウトグループが並べる、有効な子ノードを列挙します。</summary>
    template <class Layout, class Node>
    std::vector<const Node*> LayoutChildren(const Layout& layout, const Node& parent)
    {
        std::vector<const Node*> children;
        for (const auto& child : layout.objects)
            if (child.parentId == parent.id && child.rectTransform && child.rectTransform->enabled)
                children.push_back(&child);
        return children;
    }
    /// <summary>親グループの規則に従い、ノードの大きさと位置を決定します。</summary>
    template <class Layout, class Node>
    RectTransformComponent ArrangedRect(const Layout& layout, const Node& parent, const Node& node,
                                        const UiRect& bounds, RectTransformComponent rect, const UiState& state)
    {
        if (!parent.layoutGroup || !parent.layoutGroup->enabled)
            return rect;
        const auto& group = *parent.layoutGroup;
        const auto children = LayoutChildren(layout, parent);
        const auto found = std::find(children.begin(), children.end(), &node);
        if (found == children.end())
            return rect;
        const auto index = static_cast<size_t>(found - children.begin());
        const std::array<float, 2> available{
            std::max(0.0f, bounds.size[0] / bounds.scale - group.padding[0] - group.padding[2]),
            std::max(0.0f, bounds.size[1] / bounds.scale - group.padding[1] - group.padding[3])};
        rect.anchorMin = rect.anchorMax = rect.pivot = {0, 0};
        rect.position = {group.padding[0], group.padding[1]};
        if (group.direction == "grid")
        {
            rect.size = group.cellSize;
            rect.position[0] += static_cast<float>(index % group.columns) * (group.cellSize[0] + group.spacing[0]);
            rect.position[1] += static_cast<float>(index / group.columns) * (group.cellSize[1] + group.spacing[1]);
        }
        else
        {
            const size_t axis = group.direction == "horizontal" ? 0 : 1;
            const bool expand = axis == 0 ? group.expandWidth : group.expandHeight;
            if (expand)
                rect.size[axis] =
                    std::max(0.0f, (available[axis] - group.spacing[axis] * static_cast<float>(children.size() - 1)) /
                                       static_cast<float>(children.size()));
            if (axis == 0 && group.expandHeight)
                rect.size[1] = available[1];
            if (axis == 1 && group.expandWidth)
                rect.size[0] = available[0];
            for (size_t i = 0; i < index; ++i)
            {
                float opacity = 1;
                rect.position[axis] +=
                    (expand ? rect.size[axis] : AnimatedRect(*children[i], state, opacity).size[axis]) +
                    group.spacing[axis];
            }
        }
        return rect;
    }
    /// <summary>親の回転を含めた表示領域で子を切り抜きます。</summary>
    inline void ClipChildren(UiRect& rect)
    {
        const float halfX = rect.size[0] * .5f, halfY = rect.size[1] * .5f;
        const float extentX = std::abs(std::cos(rect.rotation)) * halfX + std::abs(std::sin(rect.rotation)) * halfY;
        const float extentY = std::abs(std::sin(rect.rotation)) * halfX + std::abs(std::cos(rect.rotation)) * halfY;
        const float centerX = rect.position[0] + halfX, centerY = rect.position[1] + halfY;
        rect.clip = {std::max(rect.clip[0], centerX - extentX), std::max(rect.clip[1], centerY - extentY),
                     std::min(rect.clip[2], centerX + extentX), std::min(rect.clip[3], centerY + extentY)};
    }
    /// <summary>最上位Canvasの参照サイズと拡縮率を反映します。</summary>
    void ApplyCanvas(const auto& node, unsigned int width, unsigned int height, bool& hasCanvas, UiRect& r)
    {
        if (node.canvas && hasCanvas)
            r.visible = r.visible && node.canvas->enabled;
        if (node.canvas && !hasCanvas)
        {
            hasCanvas = true;
            const auto& c = *node.canvas;
            r.scale = c.scaleWithScreen ? std::min(width / c.referenceSize[0], height / c.referenceSize[1]) : 1;
            r.size = c.scaleWithScreen
                         ? std::array<float, 2>{c.referenceSize[0] * r.scale, c.referenceSize[1] * r.scale}
                         : std::array<float, 2>{static_cast<float>(width), static_cast<float>(height)};
            r.position = {(width - r.size[0]) * 0.5f, (height - r.size[1]) * 0.5f};
            r.visible = c.enabled;
        }
    }

    /// <summary>スクロールと親グループを含めた配置を子へ伝播します。</summary>
    template <class Layout, class Node>
    void ApplyResolvedRect(const Layout& layout, const Node& node, const Node* parent, UiRect& r, const UiState& state,
                           const UiState& effective)
    {
        if (node.rectTransform)
        {
            float opacity = 1;
            auto rect = AnimatedRect(node, effective, opacity);
            if (parent)
                rect = ArrangedRect(layout, *parent, node, r, rect, effective);
            if (parent && parent->scrollView && parent->scrollView->enabled)
            {
                const auto found = state.scrollOffsets.find(parent->id);
                const auto offset = found == state.scrollOffsets.end() ? parent->scrollView->offset : found->second;
                for (size_t axis = 0; axis < 2; ++axis)
                    rect.position[axis] -= offset[axis];
            }
            r = Child(r, rect, effective);
            r.opacity *= opacity;
        }
    }

    /// <summary>CanvasとUIコントロールが宣言した既定状態を集めます。</summary>
    UiState DefaultState(const auto& layout)
    {
        UiState state;
        for (const auto& p : layout.objects)
        {
            if (p.canvas && p.canvas->enabled)
                for (const auto& [key, value] : p.canvas->stateDefaults)
                    state.values.try_emplace(key, value);
            if (p.slider)
                state.values.try_emplace(p.slider->binding.empty() ? p.id : p.slider->binding, p.slider->value);
            if (p.toggle)
                state.values.try_emplace(p.toggle->binding.empty() ? p.id : p.toggle->binding,
                                         p.toggle->value ? 1.0f : 0.0f);
            if (p.inputField)
                state.strings.try_emplace(p.inputField->binding.empty() ? p.id : p.inputField->binding,
                                          p.inputField->text);
            if (p.scrollView)
                state.scrollOffsets.try_emplace(p.id, p.scrollView->offset);
        }
        return state;
    }
    /// <summary>ノードの親子関係と指定viewの状態から最終配置を解決します。</summary>
    template <class Layout, class Node>
    UiRect Resolve(const Layout& layout, const Node& object, unsigned int width, unsigned int height,
                   const UiState& state)
    {
        auto effective = DefaultState(layout);
        for (const auto& [key, value] : state.values)
            effective.values[key] = value;
        effective.visibility = state.visibility;
        std::vector<const Node*> chain;
        const auto* p = &object;
        while (p && chain.size() <= layout.objects.size())
        {
            chain.push_back(p);
            p = Find(layout, p->parentId);
        }
        UiRect r;
        r.visible = false;
        bool hasCanvas = false;
        const Node* parent = nullptr;
        for (auto i = chain.rbegin(); i != chain.rend(); ++i)
        {
            const auto& node = **i;
            ApplyCanvas(node, width, height, hasCanvas, r);
            ApplyResolvedRect(layout, node, parent, r, state, effective);
            const auto v = state.visibility.find(node.id);
            if (v != state.visibility.end())
                r.visible = r.visible && v->second;
            if (&node != &object &&
                ((node.mask && node.mask->enabled) || (node.scrollView && node.scrollView->enabled)))
                ClipChildren(r);
            parent = &node;
        }
        return r;
    }
    /// <summary>入力欄のフォーカス・秘密表示を反映した文字列を取得します。</summary>
    std::string InputText(const auto& p, const UiState& state)
    {
        if (!p.inputField)
            return p.text ? p.text->text : std::string{};
        const auto& c = *p.inputField;
        const auto key = c.binding.empty() ? p.id : c.binding;
        const auto found = state.strings.find(key);
        std::string text = found == state.strings.end() ? c.text : found->second;
        if (text.empty())
            return c.placeholder;
        if (c.password)
        {
            const auto count = std::count_if(text.begin(), text.end(),
                                             [](const unsigned char character)
                                             {
                                                 return (character & 0xc0) != 0x80;
                                             });
            text.assign(count, '*');
        }
        if (state.focused == p.id && !c.readOnly)
            text += '|';
        return text;
    }
}
