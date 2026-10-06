#pragma once
#include <cmath>
#include <cstdint>
#include <limits>

namespace Editor
{
    class PlayState final
    {
    public:
        enum class Mode { Editing, Playing, Paused };
        Mode Current() const { return mode_; }
        bool IsEditing() const { return mode_==Mode::Editing; }
        bool CanPlay() const { return mode_!=Mode::Playing; }
        bool CanPause() const { return mode_==Mode::Playing; }
        bool CanStop() const { return !IsEditing(); }
        bool CanStep() const { return mode_==Mode::Paused; }
        static constexpr double StepSeconds=1.0/60.0;
        double Elapsed() const { return elapsed_; }
        uint64_t Updates() const { return updates_; }
        const char* Label() const
        {
            switch (mode_)
            {
            case Mode::Editing: return "編集中";
            case Mode::Playing: return "再生中";
            case Mode::Paused: return "一時停止中";
            }
            return "編集中";
        }
        bool Play()
        {
            if (!CanPlay()) return false;
            mode_=Mode::Playing;
            return true;
        }
        bool Pause()
        {
            if (!CanPause()) return false;
            mode_=Mode::Paused;
            return true;
        }
        bool Stop()
        {
            if (!CanStop()) return false;
            mode_=Mode::Editing;
            elapsed_=0;
            updates_=0;
            return true;
        }
        // Only accepted ticks advance the runtime. Pause and invalid timing leave it frozen.
        bool Advance(double seconds)
        {
            return mode_==Mode::Playing && Tick(seconds);
        }
        bool Step() { return CanStep() && Tick(StepSeconds); }
    private:
        bool Tick(double seconds)
        {
            if (!std::isfinite(seconds) || seconds<=0 ||
                !std::isfinite(elapsed_+seconds) || updates_==std::numeric_limits<uint64_t>::max()) return false;
            elapsed_+=seconds;
            ++updates_;
            return true;
        }
        Mode mode_=Mode::Editing;
        double elapsed_=0;
        uint64_t updates_=0;
    };
}
