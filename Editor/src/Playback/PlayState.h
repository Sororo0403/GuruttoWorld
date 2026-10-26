#pragma once
#include <cmath>
#include <cstdint>
#include <limits>

namespace Editor
{
    class PlayState final
    {
    public:
        enum class Mode
        {
            Editing,
            Playing,
            Paused
        };
        Mode Current() const
        {
            return mode_;
        }
        bool IsEditing() const
        {
            return mode_ == Mode::Editing;
        }
        bool CanPlay() const
        {
            return mode_ != Mode::Playing;
        }
        bool CanPause() const
        {
            return mode_ == Mode::Playing;
        }
        bool CanStop() const
        {
            return !IsEditing();
        }
        bool CanStep() const
        {
            return mode_ == Mode::Paused;
        }
        static constexpr double StepSeconds = 1.0 / 60.0;
        double Elapsed() const
        {
            return elapsed_;
        }
        uint64_t Updates() const
        {
            return updates_;
        }
        /// <summary>現在の状態の表示名を返します。</summary>
        const char* Label() const;
        /// <summary>再生を開始します。</summary>
        bool Play();
        /// <summary>再生を一時停止します。</summary>
        bool Pause();
        /// <summary>再生を停止します。</summary>
        bool Stop();
        /// <summary>
        /// 受理した更新だけが実行状態を進めます。一時停止中や時刻が無効な場合は進めません。
        /// </summary>
        bool Advance(double seconds)
        {
            return mode_ == Mode::Playing && Tick(seconds);
        }
        bool Step(double seconds = StepSeconds)
        {
            return CanStep() && Tick(seconds);
        }

    private:
        /// <summary>指定した時間だけ状態を進めます。</summary>
        bool Tick(double seconds);
        Mode mode_ = Mode::Editing;
        double elapsed_ = 0;
        uint64_t updates_ = 0;
    };
}
