#pragma once
#include <chrono>

namespace fms
{
    // Small, deterministic helper for measuring wall-clock time only while
    // playback is active. Pause/resume boundaries can be driven by fb2k callbacks.
    class PlaybackTimeAccumulator
    {
    public:
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;

        void resume(TimePoint now = Clock::now())
        {
            if (!m_running)
            {
                m_started = now;
                m_running = true;
            }
        }

        void pause(TimePoint now = Clock::now())
        {
            if (m_running)
            {
                m_accumulated += now - m_started;
                m_running = false;
            }
        }

        double elapsed(TimePoint now = Clock::now()) const
        {
            const auto total = m_accumulated + (m_running ? now - m_started : Clock::duration::zero());
            return std::chrono::duration<double>(total).count();
        }

        double finish(TimePoint now = Clock::now())
        {
            pause(now);
            const double result = std::chrono::duration<double>(m_accumulated).count();
            m_accumulated = Clock::duration::zero();
            return result;
        }

        void reset()
        {
            m_accumulated = Clock::duration::zero();
            m_running = false;
        }

    private:
        Clock::duration m_accumulated{};
        TimePoint m_started{};
        bool m_running{false};
    };
} // namespace fms
