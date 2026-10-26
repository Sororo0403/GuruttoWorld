#include <Engine/Graphics/Resources/GpuProfiler.h>

namespace Engine
{
    GpuScope::GpuScope(ID3D12GraphicsCommandList* commands, const char* name)
    {
        if (!commands)
            return;
        GpuProfileContext context;
        UINT bytes = sizeof(context);
        if (FAILED(commands->GetPrivateData(Key, &bytes, &context)) || bytes != sizeof(context) || !context.heap ||
            !context.records || context.records->size() >= GpuProfileContext::Capacity)
            return;
        query_ = context.base + 2 + static_cast<UINT>(context.records->size()) * 2;
        context.records->push_back({name, query_});
        commands_ = commands;
        heap_ = context.heap;
        commands_->EndQuery(heap_, D3D12_QUERY_TYPE_TIMESTAMP, query_);
    }
}
