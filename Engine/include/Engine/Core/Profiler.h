#pragma once
#include <chrono>
#include <map>
#include <string>
#include <vector>
namespace Engine
{
    struct ProfileSample
    {
        std::string name;
        double milliseconds = 0;
        unsigned int calls = 0;
    };
    class CpuProfiler final
    {
        static auto& Samples()
        {
            static thread_local std::map<std::string, ProfileSample> samples;
            return samples;
        }

    public:
        /// <summary>新しい対象を追加します。</summary>
        static void Add(const char* name, double milliseconds);
        /// <summary>蓄積した結果を取得して消去します。</summary>
        static std::vector<ProfileSample> Take();
    };
    class CpuScope final
    {
        const char* name_;
        std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();

    public:
        explicit CpuScope(const char* name) : name_(name) {}
        CpuScope(const CpuScope&) = delete;
        CpuScope& operator=(const CpuScope&) = delete;
        /// <summary>計測区間の処理時間を記録します。</summary>
        ~CpuScope() noexcept;
    };
}
