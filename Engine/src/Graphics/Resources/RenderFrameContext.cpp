#include <Engine/Graphics/Resources/RenderFrameContext.h>

namespace Engine
{
    auto RenderFrameContext::Current(ID3D12GraphicsCommandList* commands, RenderFrameContext& result) -> bool
    {
        UINT bytes = sizeof(result);
        return commands && SUCCEEDED(commands->GetPrivateData(Key, &bytes, &result)) && bytes == sizeof(result) &&
               result.slot < SlotCount;
    }
}
