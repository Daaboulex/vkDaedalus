#ifndef LATENCY_STATS_HPP_INCLUDED
#define LATENCY_STATS_HPP_INCLUDED

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace vkBasalt
{
    struct FrameStats
    {
        size_t count            = 0;
        double windowMs         = 0.0;
        double averageMs        = 0.0;
        double fps              = 0.0;
        double minMs            = 0.0;
        double maxMs            = 0.0;
        size_t slowestCount     = 0;
        double slowestAverageMs = 0.0;
    };

    enum class EffectsSpanState
    {
        EffectsOff,
        TimingOff,
        NothingTimed,
        Waiting,
        Measured
    };

    inline EffectsSpanState effectsSpanState(bool effectsOn, bool timingSetting, bool timingRuns, bool haveSpan)
    {
        if (!effectsOn)
            return EffectsSpanState::EffectsOff;
        if (!timingSetting)
            return EffectsSpanState::TimingOff;
        if (!timingRuns)
            return EffectsSpanState::NothingTimed;
        return haveSpan ? EffectsSpanState::Measured : EffectsSpanState::Waiting;
    }

    inline const char* effectsSpanWord(EffectsSpanState state)
    {
        switch (state)
        {
            case EffectsSpanState::EffectsOff: return "effects off";
            case EffectsSpanState::TimingOff: return "timing off";
            case EffectsSpanState::NothingTimed: return "nothing timed";
            case EffectsSpanState::Waiting: return "not measured yet";
            case EffectsSpanState::Measured: return "measured";
        }
        return "unknown";
    }

    inline double frameGenerationHoldMs(double averageIntervalMs, uint32_t multiplier, bool effectsAfterFrameGeneration)
    {
        return effectsAfterFrameGeneration ? averageIntervalMs * multiplier : averageIntervalMs;
    }

    struct FrameRates
    {
        double gameFps       = 0.0;
        double shownFps      = 0.0;
        bool   gameMeasured  = true;
        bool   shownMeasured = true;
    };

    inline FrameRates frameRatesOf(double timedFps, std::optional<uint32_t> multiplier, bool effectsAfterFrameGeneration)
    {
        if (!multiplier || *multiplier < 2)
            return {timedFps, timedFps, true, true};
        if (effectsAfterFrameGeneration)
            return {timedFps / *multiplier, timedFps, false, true};
        return {timedFps, timedFps * *multiplier, true, false};
    }

    inline double plotCeilingMs(double averageMs, double slowestAverageMs)
    {
        return std::max(averageMs * 2.0, slowestAverageMs * 1.1);
    }

    inline FrameStats frameStats(std::vector<double> intervalsMs)
    {
        FrameStats stats;
        if (intervalsMs.empty())
            return stats;
        stats.count = intervalsMs.size();
        for (const double interval : intervalsMs)
            stats.windowMs += interval;
        stats.averageMs                 = stats.windowMs / static_cast<double>(stats.count);
        stats.fps                       = stats.averageMs > 0.0 ? 1000.0 / stats.averageMs : 0.0;
        const auto [shortest, longest]  = std::minmax_element(intervalsMs.begin(), intervalsMs.end());
        stats.maxMs                     = *longest;
        stats.minMs                     = *shortest;
        stats.slowestCount              = (stats.count + 99) / 100;
        std::nth_element(intervalsMs.begin(), intervalsMs.begin() + static_cast<std::ptrdiff_t>(stats.slowestCount - 1), intervalsMs.end(), std::greater<>());
        for (size_t i = 0; i < stats.slowestCount; i++)
            stats.slowestAverageMs += intervalsMs[i];
        stats.slowestAverageMs /= static_cast<double>(stats.slowestCount);
        return stats;
    }

    inline constexpr std::chrono::milliseconds fpsWindow{1000};
    inline constexpr std::chrono::milliseconds statsWindow{10000};

    class PresentIntervals
    {
    public:
        static constexpr size_t capacity = 16384;

        void record(std::chrono::steady_clock::time_point at)
        {
            if (last)
            {
                const double interval = std::chrono::duration<double, std::milli>(at - *last).count();
                ring[next]            = {interval, at};
                next                  = (next + 1) % capacity;
                held                  = std::min(held + 1, capacity);
                summedMs += interval;
                intervals++;
            }
            last = at;
        }

        std::vector<double> within(std::chrono::steady_clock::time_point now, std::chrono::milliseconds span) const
        {
            std::vector<double> ordered;
            for (size_t i = 0; i < held; i++)
            {
                const Entry& entry = ring[(next + capacity - 1 - i) % capacity];
                if (entry.endedAt <= now - span)
                    break;
                ordered.push_back(entry.intervalMs);
            }
            std::reverse(ordered.begin(), ordered.end());
            return ordered;
        }

        uint64_t intervalCount() const { return intervals; }
        double   intervalSumMs() const { return summedMs; }

    private:
        struct Entry
        {
            double                                intervalMs = 0.0;
            std::chrono::steady_clock::time_point endedAt;
        };

        std::array<Entry, capacity>                          ring{};
        size_t                                               next      = 0;
        size_t                                               held      = 0;
        uint64_t                                             intervals = 0;
        double                                               summedMs  = 0.0;
        std::optional<std::chrono::steady_clock::time_point> last;
    };
}

#endif
