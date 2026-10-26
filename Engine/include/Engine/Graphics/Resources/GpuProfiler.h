#pragma once
#include <Engine/Core/Profiler.h>
#include <d3d12.h>
namespace Engine
{
    struct GpuProfileRecord
    {
        std::string name;
        UINT query = 0;
    };
    struct GpuProfileContext
    {
        ID3D12QueryHeap* heap = nullptr;
        UINT base = 0;
        std::vector<GpuProfileRecord>* records = nullptr;
        static constexpr UINT Capacity = 64, Queries = 2 + Capacity * 2;
    };
    class GpuScope final
    {
        ID3D12GraphicsCommandList* commands_ = nullptr;
        ID3D12QueryHeap* heap_ = nullptr;
        UINT query_ = 0;

    public:
        inline static constexpr GUID Key = {
            0x1a549e51, 0x1038, 0x46ac, {0x94, 0x31, 0x37, 0x33, 0xd5, 0x4e, 0x7b, 0x92}};
        /// <summary>GPUの計測区間を記録します。</summary>
        GpuScope(ID3D12GraphicsCommandList* commands, const char* name);
        GpuScope(const GpuScope&) = delete;
        GpuScope& operator=(const GpuScope&) = delete;
        ~GpuScope()
        {
            if (commands_)
                commands_->EndQuery(heap_, D3D12_QUERY_TYPE_TIMESTAMP, query_ + 1);
        }
    };
}
