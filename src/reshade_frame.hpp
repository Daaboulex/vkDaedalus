#pragma once

#include <chrono>
#include <cstdint>

namespace vkBasalt
{
    struct ReshadeFrame
    {
        uint64_t id          = 0;
        uint64_t frameCount  = 0;
        uint32_t timerMs     = 0;
        float    frameTimeMs = 0.0f;
        bool     overlayOpen = false;
    };

    struct PresentClock
    {
        using Clock = std::chrono::steady_clock;

        Clock::time_point start          = Clock::now();
        Clock::time_point last           = start;
        uint64_t          presents       = 0;
        float             lastIntervalMs = 0.0f;

        ReshadeFrame frame(bool overlayOpen) const
        {
            const auto timer = std::chrono::duration_cast<std::chrono::milliseconds>(last - start).count();
            return {presents + 1, presents, static_cast<uint32_t>(timer), lastIntervalMs, overlayOpen};
        }

        void presented(Clock::time_point now)
        {
            lastIntervalMs = std::chrono::duration<float, std::milli>(now - last).count();
            last           = now;
            presents++;
        }

        void continueFrom(const PresentClock& old)
        {
            start          = old.start;
            last           = old.last;
            lastIntervalMs = old.lastIntervalMs;
            presents       = 0;
        }
    };
}
