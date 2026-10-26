#include "AllocationProbe.h"
#include <cstdlib>
#include <malloc.h>
#include <new>

namespace
{
    thread_local bool enabled = false, denied = false;
    thread_local TestSupport::AllocationCounts counts;

    void* Allocate(size_t bytes, size_t alignment = 0)
    {
        if (enabled)
        {
            ++counts.count;
            counts.bytes += bytes;
            if (denied)
                throw std::bad_alloc();
        }
        for (;;)
        {
            void* memory = alignment ? _aligned_malloc(bytes ? bytes : 1, alignment) : std::malloc(bytes ? bytes : 1);
            if (memory)
                return memory;
            const auto handler = std::get_new_handler();
            if (!handler)
                throw std::bad_alloc();
            handler();
        }
    }
}

void* operator new(size_t bytes)
{
    return Allocate(bytes);
}
void* operator new[](size_t bytes)
{
    return Allocate(bytes);
}
void* operator new(size_t bytes, std::align_val_t alignment)
{
    return Allocate(bytes, static_cast<size_t>(alignment));
}
void* operator new[](size_t bytes, std::align_val_t alignment)
{
    return Allocate(bytes, static_cast<size_t>(alignment));
}
void operator delete(void* memory) noexcept
{
    std::free(memory);
}
void operator delete[](void* memory) noexcept
{
    std::free(memory);
}
void operator delete(void* memory, size_t) noexcept
{
    std::free(memory);
}
void operator delete[](void* memory, size_t) noexcept
{
    std::free(memory);
}
void operator delete(void* memory, std::align_val_t) noexcept
{
    _aligned_free(memory);
}
void operator delete[](void* memory, std::align_val_t) noexcept
{
    _aligned_free(memory);
}
void operator delete(void* memory, size_t, std::align_val_t) noexcept
{
    _aligned_free(memory);
}
void operator delete[](void* memory, size_t, std::align_val_t) noexcept
{
    _aligned_free(memory);
}

namespace TestSupport
{
    AllocationProbe::AllocationProbe(bool deny) noexcept
        : previousEnabled_(enabled), previousDenied_(denied), previousCounts_(counts)
    {
        enabled = true;
        denied = deny;
        counts = {};
    }
    AllocationProbe::~AllocationProbe()
    {
        enabled = previousEnabled_;
        denied = previousDenied_;
        counts = previousCounts_;
    }
    AllocationCounts AllocationProbe::Counts() const noexcept
    {
        return counts;
    }
}
