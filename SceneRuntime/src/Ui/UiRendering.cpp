#include <SceneRuntime/Ui/UiRendering.h>
#include <Engine/Graphics/Resources/RenderFrameContext.h>
#include "UiVisualFunctions.h"
#include <cstring>

namespace
{
    using namespace SceneRuntime;
    using namespace SceneRuntime::UiVisualDetail;
    void DrawPart(ID3D12GraphicsCommandList* commands, const UiDrawResources& resources, const UiNode& p,
                  const UiRect& rect, unsigned int width, unsigned int height, const UiState& state, bool text)
    {
        const auto resource = resources.parts.find(p.id + (text ? "/text" : "/image"));
        if (resource == resources.parts.end())
            return;
        Engine::SpriteDrawParameters draw;
        draw.position = rect.position;
        draw.size = rect.size;
        draw.rotation = rect.rotation;
        draw.color = text ? p.text->color : p.image->color;
        draw.uvRect = text ? std::array<float, 4>{0, 0, 1, 1} : p.image->uv;
        if (p.button && p.button->enabled)
        {
            if (state.pressed == p.id)
                draw.color = p.button->pressedColor;
            else if (state.hovered == p.id)
                draw.color = p.button->hoverColor;
        }
        draw.color[3] *= rect.opacity;
        SetClip(draw, rect);
        resource->second.sprite->Draw(commands, width, height, draw);
    }
    void DrawCanvasParts(ID3D12GraphicsCommandList* commands, const UiDrawResources& resources, const UiNode& p,
                         const UiRect& rect, const DirectX::XMFLOAT4X4& projection, const UiState& state)
    {
        for (const bool text : {false, true})
        {
            if (text ? (!p.text || !p.text->enabled) : (!p.image || !p.image->enabled))
                continue;
            const auto resource = resources.parts.find(p.id + (text ? "/text" : "/image"));
            if (resource == resources.parts.end())
                continue;
            Engine::SpriteDrawParameters draw;
            draw.position = rect.position;
            draw.size = rect.size;
            draw.rotation = rect.rotation;
            draw.color = text ? p.text->color : p.image->color;
            draw.color[3] *= rect.opacity;
            draw.uvRect = text ? std::array<float, 4>{0, 0, 1, 1} : p.image->uv;
            std::memcpy(draw.pixelConstants.data(), &projection, sizeof(projection));
            SetClip(draw, rect);
            resource->second.sceneSprite->Draw(commands, 1, 1, draw);
            if (!text && p.slider && p.slider->enabled)
            {
                const auto filled = SliderFill(p, rect, state);
                draw.position = filled.position;
                draw.size = filled.size;
                draw.color = p.slider->fillColor;
                draw.color[3] *= rect.opacity;
                resource->second.sceneSprite->Draw(commands, 1, 1, draw);
            }
        }
    }
}
namespace SceneRuntime
{
    using namespace UiVisualDetail;
    bool UiRendering::Draw(ID3D12GraphicsCommandList* commands, const UiPacket& layout,
                           const std::shared_ptr<const UiDrawResources>& resources, unsigned int width,
                           unsigned int height, const UiState& state)
    {
        if (!width || !height || !resources)
            return true;
        if (!Engine::RenderFrameContext::Retain(commands, resources))
            return false;
        for (const auto& source : layout.objects)
        {
            if (!source.rectTransform)
                continue;
            const auto p = ControlVisual(source, state);
            const auto rect = UiGeometry::Resolve(layout, source, width, height, state);
            if (!rect.visible)
                continue;
            if (p.image && p.image->enabled)
                DrawPart(commands, *resources, p, rect, width, height, state, false);
            if (p.slider && p.slider->enabled && p.image)
            {
                auto fill = p;
                fill.image->color = p.slider->fillColor;
                DrawPart(commands, *resources, fill, SliderFill(p, rect, state), width, height, state, false);
            }
            if (p.text && p.text->enabled)
                DrawPart(commands, *resources, p, rect, width, height, state, true);
        }
        return true;
    }
    bool UiRendering::DrawScene(ID3D12GraphicsCommandList* commands, const UiPacket& layout,
                                const std::shared_ptr<const UiDrawResources>& resources, const Engine::Camera& camera,
                                const UiState& state)
    {
        if (!resources)
            return true;
        if (!Engine::RenderFrameContext::Retain(commands, resources))
            return false;
        for (const auto& source : layout.objects)
        {
            if (!source.rectTransform)
                continue;
            const auto* root = &source;
            const UiNode* canvas = nullptr;
            size_t remaining = layout.objects.size() + 1;
            while (root && remaining--)
            {
                if (root->canvas)
                    canvas = root;
                const auto found = std::find_if(layout.objects.begin(), layout.objects.end(),
                                                [&](const auto& node)
                                                {
                                                    return node.id == root->parentId;
                                                });
                root = found == layout.objects.end() ? nullptr : &*found;
            }
            if (root || !canvas)
                continue;
            const auto p = UiVisualDetail::ControlVisual(source, state);
            const auto rect =
                UiGeometry::Resolve(layout, source, static_cast<unsigned int>(canvas->canvas->referenceSize[0]),
                                    static_cast<unsigned int>(canvas->canvas->referenceSize[1]), state);
            if (!rect.visible)
                continue;
            using namespace DirectX;
            XMFLOAT4X4 projection;
            XMStoreFloat4x4(&projection, XMMatrixScaling(.01f, -.01f, 1) *
                                             XMMatrixTranslation(-canvas->canvas->referenceSize[0] * .005f,
                                                                 canvas->canvas->referenceSize[1] * .005f, 0) *
                                             XMLoadFloat4x4(&canvas->matrix) * camera.GetViewMatrix() *
                                             camera.GetProjectionMatrix());
            DrawCanvasParts(commands, *resources, p, rect, projection, state);
        }
        return true;
    }
}
