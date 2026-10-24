#pragma once
#include <chrono>
#include <iostream>
#include <string_view>
#include <Engine/Core/Profiler.h>
#include <algorithm>
#include <cstdlib>

namespace RegressionTiming
{
    class CpuTotals
    {
    public:
        /// <summary>更新区間の開始前に、初期化や比較処理から残った計測値を除外します。</summary>
        void DiscardPending()
        {
            size_t length = 0;
            if (getenv_s(&length, nullptr, 0, "WP1_UPDATE_TIMING") == 0 && length != 0)
                static_cast<void>(Engine::CpuProfiler::Take());
        }
        /// <summary>任意指定の詳細計測では、描画でリセットされる前に更新処理のCPU区間を集計します。</summary>
        void Collect()
        {
            size_t length = 0;
            if (getenv_s(&length, nullptr, 0, "WP1_UPDATE_TIMING") != 0 || length == 0)
                return;
            for (const auto& sample : Engine::CpuProfiler::Take())
            {
                auto& total = samples_[sample.name];
                total.name = sample.name;
                total.milliseconds += sample.milliseconds;
                total.calls += sample.calls;
            }
        }
        /// <summary>更新処理の内訳を時間の長い順に記録します。</summary>
        ~CpuTotals()
        {
            std::vector<Engine::ProfileSample> sorted;
            for (const auto& [name, sample] : samples_)
                sorted.push_back(sample);
            std::sort(sorted.begin(), sorted.end(),
                      [](const auto& a, const auto& b)
                      {
                          return a.milliseconds > b.milliseconds;
                      });
            for (const auto& sample : sorted)
                std::cerr << "CPU updates: " << sample.name << " " << sample.milliseconds / 1000 << "s / "
                          << sample.calls << " calls\n";
        }

    private:
        std::map<std::string, Engine::ProfileSample> samples_;
    };
    struct Total
    {
        std::string_view name;
        double seconds = 0;
        unsigned int calls = 0;
        /// <summary>繰り返す検証処理の合計時間と回数を記録します。</summary>
        ~Total()
        {
            std::cerr << "Timing: " << name << " " << seconds << "s / " << calls << " calls\n";
        }
        /// <summary>開始時刻からの経過時間を合計へ追加します。</summary>
        void AddSince(std::chrono::steady_clock::time_point started)
        {
            seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            ++calls;
        }
    };
    class Scope
    {
    public:
        /// <summary>指定した検証区間の実時間を計測します。</summary>
        explicit Scope(std::string_view name) : name_(name) {}
        /// <summary>指定した集計区間へ実時間を加算します。</summary>
        explicit Scope(Total& total) : total_(&total) {}
        /// <summary>検証区間の経過秒数を標準エラーへ記録します。</summary>
        ~Scope()
        {
            if (total_)
            {
                total_->AddSince(started_);
                return;
            }
            std::cerr << "Timing: " << name_ << " "
                      << std::chrono::duration<double>(std::chrono::steady_clock::now() - started_).count() << "s\n";
        }

    private:
        std::string_view name_;
        Total* total_ = nullptr;
        std::chrono::steady_clock::time_point started_ = std::chrono::steady_clock::now();
    };
}
