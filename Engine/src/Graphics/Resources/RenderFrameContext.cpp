#include <Engine/Graphics/Resources/RenderFrameContext.h>

namespace Engine
{
    auto RenderFrameContext::Current(ID3D12GraphicsCommandList* commands, RenderFrameContext& result) -> bool
    {
        UINT bytes = sizeof(result);
        return commands && SUCCEEDED(commands->GetPrivateData(Key, &bytes, &result)) && bytes == sizeof(result) &&
               result.slot < SlotCount;
    }
    bool RenderFrameContext::Retain(ID3D12GraphicsCommandList* commands, std::shared_ptr<const void> resource)
    {
        RenderFrameContext frame;
        if (!resource || !Current(commands, frame) || !frame.resources)
            return false;
        frame.resources->push_back(std::move(resource));
        return true;
    }
}
