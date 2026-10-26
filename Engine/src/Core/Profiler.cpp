#include <Engine/Core/Profiler.h>

namespace Engine
{
    auto CpuProfiler::Add(const char* name, double milliseconds) -> void
    {
        auto& samples = Samples();
        if (samples.size() >= 128 && !samples.contains(name))
            return;
        auto& sample = samples[name];
        sample.name = name;
        sample.milliseconds += milliseconds;
        ++sample.calls;
    }

    auto CpuProfiler::Take() -> std::vector<ProfileSample>
    {
        std::vector<ProfileSample> result;
        for (auto& [name, sample] : Samples())
            result.push_back(std::move(sample));
        Samples().clear();
        return result;
    }

    CpuScope::~CpuScope() noexcept
    {
        try
        {
            CpuProfiler::Add(
                name_, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_).count());
        }
        catch (...)
        {
        }
    }
}
