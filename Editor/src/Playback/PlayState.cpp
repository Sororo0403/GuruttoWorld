#include "PlayState.h"

namespace Editor
{
    auto PlayState::Label() const -> const char*
    {
        switch (mode_)
        {
        case Mode::Editing:
            return "編集中";
        case Mode::Playing:
            return "再生中";
        case Mode::Paused:
            return "一時停止中";
        }
        return "編集中";
    }

    auto PlayState::Play() -> bool
    {
        if (!CanPlay())
            return false;
        mode_ = Mode::Playing;
        return true;
    }

    auto PlayState::Pause() -> bool
    {
        if (!CanPause())
            return false;
        mode_ = Mode::Paused;
        return true;
    }

    auto PlayState::Stop() -> bool
    {
        if (!CanStop())
            return false;
        mode_ = Mode::Editing;
        elapsed_ = 0;
        updates_ = 0;
        return true;
    }

    auto PlayState::Tick(double seconds) -> bool
    {
        if (!std::isfinite(seconds) || seconds <= 0 || !std::isfinite(elapsed_ + seconds) ||
            updates_ == std::numeric_limits<uint64_t>::max())
            return false;
        elapsed_ += seconds;
        ++updates_;
        return true;
    }
}
