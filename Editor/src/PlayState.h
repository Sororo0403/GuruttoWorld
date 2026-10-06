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
        double Elapsed() const { return elapsed_; }
        uint64_t Updates() const { return updates_; }
        const char* Label() const
        {
            switch (mode_)
            {
            case Mode::Editing: return "Editing";
            case Mode::Playing: return "Playing";
            case Mode::Paused: return "Paused";
            }
            return "Editing";
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
            if (mode_!=Mode::Playing || !std::isfinite(seconds) || seconds<=0 ||
                !std::isfinite(elapsed_+seconds) || updates_==std::numeric_limits<uint64_t>::max()) return false;
            elapsed_+=seconds;
            ++updates_;
            return true;
        }
    private:
        Mode mode_=Mode::Editing;
        double elapsed_=0;
        uint64_t updates_=0;
    };
}
