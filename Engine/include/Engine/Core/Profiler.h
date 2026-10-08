#pragma once
#include <chrono>
#include <map>
#include <string>
#include <vector>
namespace Engine {
struct ProfileSample { std::string name; double milliseconds=0; unsigned int calls=0; };
class CpuProfiler final {
    static auto& Samples() { static thread_local std::map<std::string,ProfileSample> samples; return samples; }
public:
    static void Add(const char* name,double milliseconds) {
        auto& samples=Samples(); if(samples.size()>=128 && !samples.contains(name)) return;
        auto& sample=samples[name]; sample.name=name; sample.milliseconds+=milliseconds; ++sample.calls;
    }
    static std::vector<ProfileSample> Take() {
        std::vector<ProfileSample> result; for(auto& [name,sample]:Samples()) result.push_back(std::move(sample)); Samples().clear(); return result;
    }
};
class CpuScope final {
    const char* name_; std::chrono::steady_clock::time_point start_=std::chrono::steady_clock::now();
public:
    explicit CpuScope(const char* name):name_(name){}
    CpuScope(const CpuScope&)=delete; CpuScope& operator=(const CpuScope&)=delete;
    ~CpuScope() noexcept { try { CpuProfiler::Add(name_,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start_).count()); } catch(...){} }
};
}
