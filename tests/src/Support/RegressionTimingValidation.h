#pragma once
#include "RegressionTiming.h"
#include <sstream>

namespace RegressionTimingValidation
{
    /// <summary>更新前のCPU計測値を除外し、通常計測モードでは他の計測値を消費しないことを検証します。</summary>
    inline void Run()
    {
        char* previous = nullptr;
        size_t length = 0;
        if (_dupenv_s(&previous, &length, "WP1_UPDATE_TIMING") != 0)
            throw std::runtime_error("cannot capture timing environment");
        const std::string saved = previous ? previous : "";
        free(previous);
        std::ostringstream output;
        auto* buffer = std::cerr.rdbuf(output.rdbuf());
        try
        {
            _putenv_s("WP1_UPDATE_TIMING", "1");
            {
                RegressionTiming::CpuTotals measured;
                Engine::CpuProfiler::Add("outside-window", 1000);
                measured.DiscardPending();
                Engine::CpuProfiler::Add("inside-window", 1);
                measured.Collect();
            }
            if (output.str().find("outside-window") != std::string::npos ||
                output.str().find("inside-window") == std::string::npos)
                throw std::runtime_error("CPU timing mixed initialization with updates");
            _putenv_s("WP1_UPDATE_TIMING", "");
            {
                RegressionTiming::CpuTotals disabled;
                Engine::CpuProfiler::Add("normal-profiler", 1);
                disabled.DiscardPending();
                disabled.Collect();
                const auto samples = Engine::CpuProfiler::Take();
                if (samples.size() != 1 || samples.front().name != "normal-profiler")
                    throw std::runtime_error("disabled diagnostics consumed normal profiler samples");
            }
        }
        catch (...)
        {
            std::cerr.rdbuf(buffer);
            _putenv_s("WP1_UPDATE_TIMING", saved.c_str());
            throw;
        }
        std::cerr.rdbuf(buffer);
        _putenv_s("WP1_UPDATE_TIMING", saved.c_str());
    }
}
