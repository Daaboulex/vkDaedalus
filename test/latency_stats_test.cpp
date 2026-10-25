#include "effect_timing.hpp"
#include "latency_stats.hpp"

#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

namespace
{
    int failures = 0;

    void check(bool condition, const std::string& message)
    {
        if (condition)
            return;
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }

    template <typename... Args>
    std::string printed(const char* format, Args... args)
    {
        char text[128];
        std::snprintf(text, sizeof(text), format, args...);
        return text;
    }

    std::map<std::string, std::string> exampleLines(const char* path)
    {
        std::map<std::string, std::string> lines;
        std::ifstream                      doc(path);
        check(static_cast<bool>(doc), std::string("could not read ") + path);
        std::string line;
        bool        inExample = false;
        while (std::getline(doc, line))
        {
            if (line.rfind("```", 0) == 0)
            {
                inExample = !inExample && line == "```example";
                continue;
            }
            const size_t colon = line.find(": ");
            if (inExample && colon != std::string::npos)
                check(lines.emplace(line.substr(0, colon), line.substr(colon + 2)).second, "the example key '" + line.substr(0, colon) + "' appears twice");
        }
        return lines;
    }

    void checkPrinted(const std::map<std::string, std::string>& doc, const std::string& key, const std::string& computed)
    {
        const auto it = doc.find(key);
        if (it == doc.end())
        {
            check(false, "LATENCY.md has no example line '" + key + ":'");
            return;
        }
        check(it->second == computed, "LATENCY.md says '" + key + ": " + it->second + "', the code computes '" + computed + "'");
    }

    std::vector<double> framesFrom(const std::string& text)
    {
        std::vector<double> frames;
        std::stringstream   parts(text);
        std::string         part;
        while (std::getline(parts, part, ','))
        {
            unsigned repeat   = 0;
            double   interval = 0.0;
            check(std::sscanf(part.c_str(), " %u x %lf", &repeat, &interval) == 2, "unreadable frames part '" + part + "'");
            frames.insert(frames.end(), repeat, interval);
        }
        return frames;
    }

    void checkDocument(const char* path)
    {
        using namespace vkBasalt;
        const auto doc = exampleLines(path);

        const auto frames = doc.find("frames");
        check(frames != doc.end(), "LATENCY.md has no frames example");
        if (frames != doc.end())
        {
            const FrameStats stats = frameStats(framesFrom(frames->second));
            checkPrinted(doc, "count", printed("%zu", stats.count));
            checkPrinted(doc, "window", printed("%.2f ms", stats.windowMs));
            checkPrinted(doc, "average", printed("%.2f ms", stats.averageMs));
            checkPrinted(doc, "fps", printed("%.2f", stats.fps));
            checkPrinted(doc, "min", printed("%.2f ms", stats.minMs));
            checkPrinted(doc, "max", printed("%.2f ms", stats.maxMs));
            checkPrinted(doc, "slowest 1%", printed("%zu intervals, %.2f ms", stats.slowestCount, stats.slowestAverageMs));
        }

        const auto ticks = doc.find("ticks");
        check(ticks != doc.end(), "LATENCY.md has no ticks example");
        if (ticks != doc.end())
        {
            uint64_t begin = 0, end = 0;
            unsigned bits   = 0;
            float    period = 0.0f;
            check(std::sscanf(ticks->second.c_str(), "%" SCNu64 " to %" SCNu64 ", %u valid bits, period %f ns", &begin, &end, &bits, &period) == 4,
                  "unreadable ticks line");
            checkPrinted(doc, "delta", printed("%" PRIu64 " ticks", effectTimestampDelta(begin, end, bits)));
            checkPrinted(doc, "time", printed("%.2f ms", effectTimestampMilliseconds(begin, end, bits, period)));
        }

        for (const char* order : {"before", "after"})
        {
            const std::string suffix = std::string(order) == "after" ? " after" : "";
            const auto        rates  = doc.find("rates" + suffix);
            check(rates != doc.end(), "LATENCY.md has no rates" + suffix + " example");
            if (rates == doc.end())
                continue;
            double   timed      = 0.0;
            unsigned multiplier = 0;
            char     word[16]   = {};
            check(std::sscanf(rates->second.c_str(), "%lf FPS timed, multiplier %u, effects %15s", &timed, &multiplier, word) == 3,
                  "unreadable rates" + suffix + " line");
            const FrameRates computed = frameRatesOf(timed, multiplier, std::string(word) == "after");
            checkPrinted(doc, "game" + suffix, printed("%.0f FPS, %s", computed.gameFps, computed.gameMeasured ? "measured" : "estimated"));
            checkPrinted(doc, "shown" + suffix, printed("%.0f FPS, %s", computed.shownFps, computed.shownMeasured ? "measured" : "estimated"));
        }

        const auto smoothing = doc.find("smoothing");
        check(smoothing != doc.end(), "LATENCY.md has no smoothing example");
        if (smoothing != doc.end())
        {
            float previous = 0.0f, sample = 0.0f;
            check(std::sscanf(smoothing->second.c_str(), "previous %f ms, sample %f ms", &previous, &sample) == 2, "unreadable smoothing line");
            checkPrinted(doc, "smoothed", printed("%.2f ms", smoothEffectTiming(previous, sample, true)));
            const double weight = smoothEffectTiming(0.0f, 1.0f, true);
            checkPrinted(doc, "older than 15 frames", printed("%.2f%%", 100.0 * std::pow(1.0 - weight, 15)));
        }
    }

    void checkStatistics()
    {
        using namespace vkBasalt;
        const FrameStats none = frameStats({});
        check(none.count == 0 && none.fps == 0.0 && none.slowestCount == 0, "no intervals give empty statistics");

        const FrameStats three = frameStats({30.0, 10.0, 20.0});
        check(three.count == 3 && three.windowMs == 60.0 && three.averageMs == 20.0, "three intervals: window 60, average 20");
        check(three.fps == 50.0 && three.minMs == 10.0 && three.maxMs == 30.0, "three intervals: 50 FPS, min 10, max 30");
        check(three.slowestCount == 1 && three.slowestAverageMs == 30.0, "under 100 intervals the slowest 1% is the slowest one");

        std::vector<double> hitch(101, 10.0);
        hitch[50] = 600.0;
        const FrameStats kept = frameStats(hitch);
        check(kept.maxMs == 600.0 && kept.count == 101, "a 600 ms interval is kept, not dropped");
        check(kept.slowestCount == 2 && kept.slowestAverageMs == 305.0, "101 intervals: the slowest 1% is the mean of the two longest");
    }

    void checkIntervals()
    {
        using namespace vkBasalt;
        using std::chrono::milliseconds;
        const auto       start = std::chrono::steady_clock::time_point{} + milliseconds(1000);
        PresentIntervals presents;
        const auto       always = std::chrono::hours(1000);
        check(presents.within(start, always).empty() && presents.intervalCount() == 0, "no present gives no interval");
        presents.record(start);
        check(presents.within(start, always).empty(), "the first present gives no interval");
        presents.record(start + milliseconds(10));
        presents.record(start + milliseconds(25));
        const auto two = presents.within(start + milliseconds(25), always);
        check(two.size() == 2 && two[0] == 10.0 && two[1] == 15.0, "two intervals in present order");

        PresentIntervals steady;
        for (int i = 0; i <= 30; i++)
            steady.record(start + milliseconds(100 * i));
        const auto now    = start + milliseconds(3000);
        const auto second = steady.within(now, fpsWindow);
        check(second.size() == 10 && frameStats(second).windowMs == 1000.0, "the last second holds the ten 100 ms frames that ended in it, and covers exactly 1 s");
        check(steady.within(now, statsWindow).size() == 30, "a window longer than the run holds every interval since the first present");
        check(steady.within(now + milliseconds(500), fpsWindow).size() == 5, "a frame that ended before the window starts is left out");

        PresentIntervals full;
        for (size_t i = 0; i <= PresentIntervals::capacity + 1; i++)
            full.record(start + milliseconds(static_cast<long>(i * (i + 1) / 2)));
        const auto last = start + milliseconds(static_cast<long>((PresentIntervals::capacity + 1) * (PresentIntervals::capacity + 2) / 2));
        const auto kept = full.within(last, always);
        check(kept.size() == PresentIntervals::capacity, "the ring keeps its capacity");
        check(kept.front() == 2.0 && kept.back() == static_cast<double>(PresentIntervals::capacity + 1), "a full ring drops the oldest interval first");
        check(full.intervalCount() == PresentIntervals::capacity + 1, "every interval is counted, kept or not");
        const double every = static_cast<double>((PresentIntervals::capacity + 1) * (PresentIntervals::capacity + 2) / 2);
        check(full.intervalSumMs() == every, "the summed time covers every interval, kept or not");
    }
}

int main(int argc, char** argv)
{
    check(argc == 2, "usage: latency-stats-test <LATENCY.md>");
    checkStatistics();
    checkIntervals();
    check(vkBasalt::frameGenerationHoldMs(8.0, 2, true) == 16.0 && vkBasalt::frameGenerationHoldMs(6.0, 3, true) == 18.0,
          "with effects after frame generation, the layer sees every generated frame, so one real frame is the average times the multiplier");
    check(vkBasalt::effectsSpanState(false, true, true, true) == vkBasalt::EffectsSpanState::EffectsOff,
          "with the effects switched off the span is not measured, though a last figure exists");
    check(vkBasalt::effectsSpanState(true, true, false, false) == vkBasalt::EffectsSpanState::NothingTimed,
          "with timing on and nothing to time, the page says so instead of asking to turn timing on");
    check(vkBasalt::effectsSpanState(true, false, false, false) == vkBasalt::EffectsSpanState::TimingOff &&
              vkBasalt::effectsSpanState(true, true, true, true) == vkBasalt::EffectsSpanState::Measured,
          "timing off asks for it, and a span that came back is measured");
    {
        const std::string effectsOff   = vkBasalt::effectsSpanWord(vkBasalt::EffectsSpanState::EffectsOff);
        const std::string timingOff    = vkBasalt::effectsSpanWord(vkBasalt::EffectsSpanState::TimingOff);
        const std::string nothingTimed = vkBasalt::effectsSpanWord(vkBasalt::EffectsSpanState::NothingTimed);
        const std::string waiting      = vkBasalt::effectsSpanWord(vkBasalt::EffectsSpanState::Waiting);
        check(effectsOff != timingOff && effectsOff != nothingTimed && effectsOff != waiting && timingOff != nothingTimed &&
                  timingOff != waiting && nothingTimed != waiting && timingOff.find("timing") != std::string::npos,
              "each unmeasured state shows its own word, so timing off never reads as the effects being off");
    }
    check(vkBasalt::frameGenerationHoldMs(16.0, 2, false) == 16.0, "with effects before it, one real frame is the average");
    {
        const vkBasalt::FrameRates none = vkBasalt::frameRatesOf(60.0, std::nullopt, false);
        check(none.gameFps == 60.0 && none.shownFps == 60.0 && none.gameMeasured && none.shownMeasured,
              "without frame generation the game's frame rate is the one shown, both measured");
        const vkBasalt::FrameRates off = vkBasalt::frameRatesOf(60.0, 1u, true);
        check(off.gameFps == 60.0 && off.shownFps == 60.0, "at multiplier 1 lsfg-vk passes frames through, so nothing is generated");
        const vkBasalt::FrameRates before = vkBasalt::frameRatesOf(60.0, 3u, false);
        check(before.gameFps == 60.0 && before.gameMeasured && before.shownFps == 180.0 && !before.shownMeasured,
              "with effects before frame generation the layer times the game's frames, and the frames shown are estimated from the multiplier");
        const vkBasalt::FrameRates after = vkBasalt::frameRatesOf(180.0, 3u, true);
        check(after.shownFps == 180.0 && after.shownMeasured && after.gameFps == 60.0 && !after.gameMeasured,
              "with effects after it the layer times every frame shown, and the game's own rate is estimated");
        const vkBasalt::FrameStats steady = vkBasalt::frameStats(std::vector<double>(1000, 16.0));
        check(vkBasalt::plotCeilingMs(steady.averageMs, steady.slowestAverageMs) == 32.0, "steady frames sit at the middle of the plot");
        std::vector<double> hitch(999, 16.0);
        hitch.push_back(1000.0);
        const vkBasalt::FrameStats hitched = vkBasalt::frameStats(hitch);
        check(vkBasalt::plotCeilingMs(hitched.averageMs, hitched.slowestAverageMs) < 200.0,
              "one long hitch does not stretch the plot so far that every other frame lies flat at the bottom");
    }
    if (argc == 2)
        checkDocument(argv[1]);
    return failures == 0 ? 0 : 1;
}
