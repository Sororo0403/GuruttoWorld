#include <Engine/Graphics/Resources/RenderTargetBinding.h>

namespace Engine
{
    auto RenderTargetBinding::Bind(ID3D12GraphicsCommandList* commands) const -> void
    {
        commands->OMSetRenderTargets(color.ptr ? 1 : 0, color.ptr ? &color : nullptr, FALSE,
                                     depth.ptr ? &depth : nullptr);
        commands->RSSetViewports(1, &viewport);
        commands->RSSetScissorRects(1, &scissor);
        commands->SetPrivateData(Key, sizeof(*this), this);
    }
}
